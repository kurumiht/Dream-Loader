#pragma once
#include <windows.h>
#include <string>
#include <unordered_map>
#include <functional>
#include <thread>
#include <atomic>
#include <mutex>
#include <chrono>
#include "DreamLog.hpp"
#include "DreamCrashGuard.hpp"
#include "DreamScheduler.hpp"
#include "DreamDispatcher.hpp"

namespace dreamkurumi {

// ============================================================================
// DreamWatcher - Real-Time File System Monitor & Config Hot-Reloader
// Watches configuration files and triggers hot-reloads without restarting game
// ============================================================================
class DreamWatcher {
public:
    using ChangeCallback = std::function<void(const std::string& filepath)>;

    struct WatchedFile {
        std::string filepath;
        FILETIME lastWriteTime{0, 0};
        ChangeCallback callback;
        std::chrono::steady_clock::time_point lastTriggered;
    };

    static DreamWatcher& Instance() {
        static DreamWatcher s_instance;
        return s_instance;
    }

    // Register a file to monitor for changes
    void WatchFile(const std::string& filepath, ChangeCallback callback) {
        std::lock_guard<std::mutex> lock(m_mutex);
        WatchedFile wf;
        wf.filepath = filepath;
        wf.callback = callback;
        wf.lastWriteTime = GetFileWriteTime(filepath);
        wf.lastTriggered = std::chrono::steady_clock::now();
        m_watchedFiles[filepath] = wf;
        DREAM_LOG_INFO("DreamWatcher: Watching file '%s' for live hot-reload", filepath.c_str());

        // Automatically start the background watcher if not already running
        if (!m_running.load()) {
            Start();
        }
    }

    // Stop monitoring a specific file
    void UnwatchFile(const std::string& filepath) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_watchedFiles.erase(filepath);
        DREAM_LOG_INFO("DreamWatcher: Stopped watching '%s'", filepath.c_str());
    }

    // Start background watcher thread
    void Start(int pollIntervalMs = 250) {
        if (m_running.load()) return;
        m_running.store(true);
        m_workerThread = std::thread([this, pollIntervalMs]() {
            while (m_running.load()) {
                CheckFiles();
                std::this_thread::sleep_for(std::chrono::milliseconds(pollIntervalMs));
            }
        });
        DREAM_LOG_INFO("DreamWatcher: File system hot-reload listener started (Interval: %d ms)", pollIntervalMs);
    }

    // Stop background watcher thread cleanly
    void Stop() {
        if (!m_running.load()) return;
        m_running.store(false);
        if (m_workerThread.joinable()) {
            if (m_workerThread.get_id() != std::this_thread::get_id()) {
                m_workerThread.join();
            } else {
                m_workerThread.detach();
            }
        }
        DREAM_LOG_INFO("DreamWatcher: File system watcher stopped");
    }

    bool IsRunning() const {
        return m_running.load();
    }

    bool IsWatching(const std::string& filepath) {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_watchedFiles.find(filepath) != m_watchedFiles.end();
    }

    // Explicit synchronous check (useful for tests or immediate validation)
    void CheckFiles() {
        std::vector<std::pair<std::string, ChangeCallback>> toNotify;
        auto now = std::chrono::steady_clock::now();

        {
            std::lock_guard<std::mutex> lock(m_mutex);
            for (auto& [path, wf] : m_watchedFiles) {
                FILETIME currentWriteTime = GetFileWriteTime(path);
                if (currentWriteTime.dwLowDateTime == 0 && currentWriteTime.dwHighDateTime == 0) {
                    continue; // File might not exist yet
                }

                // Initial time assignment
                if (wf.lastWriteTime.dwLowDateTime == 0 && wf.lastWriteTime.dwHighDateTime == 0) {
                    wf.lastWriteTime = currentWriteTime;
                    continue;
                }

                // Check if file was modified
                if (CompareFileTime(&currentWriteTime, &wf.lastWriteTime) != 0) {
                    // Debounce: ensure at least 200ms has elapsed since previous trigger
                    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - wf.lastTriggered).count();
                    if (elapsed > 200) {
                        wf.lastWriteTime = currentWriteTime;
                        wf.lastTriggered = now;
                        if (wf.callback) {
                            toNotify.emplace_back(path, wf.callback);
                        }
                    }
                }
            }
        }

        // Notify outside lock to prevent deadlocks
        for (const auto& [path, cb] : toNotify) {
            DREAM_LOG_INFO("DreamWatcher: Detected change in '%s'! Triggering hot-reload...", path.c_str());
            DreamCrashGuard::ExecuteSafely("FileWatcherCallback", [&]() {
                cb(path);
            });
        }
    }

    // Watch a mod DLL with shadow-copy loading to allow zero-restart re-compilation
    bool WatchAndShadowReload(const std::string& originalDllPath,
                              std::function<void(HMODULE hMod)> onLoad,
                              std::function<void(HMODULE hOld)> onUnload) {
        if (originalDllPath.empty() || !onLoad) return false;

        CreateDirectoryA("mods/.shadow", nullptr);
        std::string shadowPath = "mods/.shadow/shadow_" + std::to_string(GetTickCount64()) + "_" + GetFilenameOnly(originalDllPath);
        if (!CopyFileA(originalDllPath.c_str(), shadowPath.c_str(), FALSE)) {
            DREAM_LOG_WARN("DreamWatcher: Failed to create initial shadow copy of '%s'", originalDllPath.c_str());
            return false;
        }

        HMODULE hMod = LoadLibraryA(shadowPath.c_str());
        if (!hMod) {
            DREAM_LOG_ERROR("DreamWatcher: Failed to LoadLibrary shadow copy '%s'", shadowPath.c_str());
            return false;
        }

        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_shadowModules[originalDllPath] = { originalDllPath, shadowPath, hMod, 1, std::make_shared<std::atomic<int32_t>>(0) };
        }

        DREAM_LOG_INFO("DreamWatcher: Shadow-loaded '%s' -> '%s' (Handle: %p)",
                       originalDllPath.c_str(), shadowPath.c_str(), hMod);

        DreamCrashGuard::ExecuteSafely("ShadowLoadInit", [&]() {
            onLoad(hMod);
        });

        // Watch original DLL for new compilation builds
        WatchFile(originalDllPath, [this, onLoad, onUnload](const std::string& changedPath) {
            // Wait deterministically for linker to finish writing and release file lock
            if (!WaitForLinkerReady(changedPath)) {
                DREAM_LOG_WARN("DreamWatcher: Linker file lock timeout for '%s', aborting reload", changedPath.c_str());
                return;
            }

            ShadowModuleInfo info;
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                auto it = m_shadowModules.find(changedPath);
                if (it == m_shadowModules.end()) return;
                info = it->second;
            }

            DREAM_LOG_INFO("DreamWatcher: Hot-Reloading mod '%s' (Generation #%llu)...",
                           changedPath.c_str(), info.reloadCount + 1);

            std::string modFilename = GetFilenameOnly(changedPath);
            size_t dotPos = modFilename.find_last_of('.');
            std::string modStem = (dotPos != std::string::npos) ? modFilename.substr(0, dotPos) : modFilename;

            // 1. Purge scheduled tasks and central dispatcher hooks owned by this mod
            DreamScheduler::Instance().CancelByOwner(modStem);
            DreamScheduler::Instance().CancelByOwner(modFilename);
            DreamCentralDispatcher::Instance().CancelByOwner(modStem);
            DreamCentralDispatcher::Instance().CancelByOwner(modFilename);

            // 2. Unload old instance
            if (onUnload && info.hModule) {
                DreamCrashGuard::ExecuteSafely("ShadowUnload", [&]() {
                    onUnload(info.hModule);
                });
            }

            // Wait for in-flight hook callbacks to drain before unmapping module
            auto tStart = std::chrono::steady_clock::now();
            while ((info.inFlight && info.inFlight->load() > 0) ||
                   DreamCentralDispatcher::Instance().GetTotalInFlightCount() > 0) {
                if (std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - tStart).count() > 500) {
                    DREAM_LOG_WARN("DreamWatcher: In-flight calls drained timeout (%d active, dispatcher: %d), skipping FreeLibrary to prevent crash",
                                   info.inFlight ? info.inFlight->load() : 0,
                                   DreamCentralDispatcher::Instance().GetTotalInFlightCount());
                    break;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }

            bool stillActive = (info.inFlight && info.inFlight->load() > 0) ||
                               (DreamCentralDispatcher::Instance().GetTotalInFlightCount() > 0);
            if (!stillActive) {
                if (info.hModule) {
                    FreeLibrary(info.hModule);
                }
                DeleteFileA(info.currentShadowPath.c_str());
            } else {
                DREAM_LOG_WARN("DreamWatcher: Preserving old module handle %p safely without FreeLibrary", info.hModule);
            }

            // 2. Create new shadow copy
            std::string newShadow = "mods/.shadow/shadow_" + std::to_string(GetTickCount64()) + "_" + GetFilenameOnly(changedPath);
            if (!CopyFileA(changedPath.c_str(), newShadow.c_str(), FALSE)) {
                DREAM_LOG_ERROR("DreamWatcher: Failed to create new shadow copy '%s'", newShadow.c_str());
                return;
            }

            HMODULE hNewMod = LoadLibraryA(newShadow.c_str());
            if (!hNewMod) {
                DREAM_LOG_ERROR("DreamWatcher: Failed to LoadLibrary new shadow '%s'", newShadow.c_str());
                return;
            }

            {
                std::lock_guard<std::mutex> lock(m_mutex);
                m_shadowModules[changedPath] = { changedPath, newShadow, hNewMod, info.reloadCount + 1, std::make_shared<std::atomic<int32_t>>(0) };
            }

            // 3. Initialize new instance
            DreamCrashGuard::ExecuteSafely("ShadowLoadNew", [&]() {
                onLoad(hNewMod);
            });

            DREAM_LOG_INFO("DreamWatcher: Successfully Hot-Reloaded '%s' (Generation #%llu, Handle: %p)",
                           changedPath.c_str(), info.reloadCount + 1, hNewMod);
        });

        return true;
    }

private:
    struct ShadowModuleInfo {
        std::string originalPath;
        std::string currentShadowPath;
        HMODULE hModule{nullptr};
        uint64_t reloadCount{0};
        std::shared_ptr<std::atomic<int32_t>> inFlight;
    };

    static bool WaitForLinkerReady(const std::string& path, int maxAttempts = 50) {
        for (int i = 0; i < maxAttempts; ++i) {
            HANDLE h = CreateFileA(path.c_str(), GENERIC_READ, 0 /* exclusive */, nullptr, OPEN_EXISTING, 0, nullptr);
            if (h != INVALID_HANDLE_VALUE) {
                DWORD bytesRead = 0;
                IMAGE_DOS_HEADER dos{};
                if (ReadFile(h, &dos, sizeof(dos), &bytesRead, nullptr) && bytesRead == sizeof(dos) && dos.e_magic == IMAGE_DOS_SIGNATURE) {
                    CloseHandle(h);
                    return true;
                }
                CloseHandle(h);
            }
            if (GetLastError() != ERROR_SHARING_VIOLATION && GetLastError() != ERROR_ACCESS_DENIED) {
                // If other error, break
            }
            Sleep(20 + i * 10);
        }
        return false;
    }

    static std::string GetFilenameOnly(const std::string& path) {
        size_t idx = path.find_last_of("\\/");
        if (idx != std::string::npos) return path.substr(idx + 1);
        return path;
    }

    DreamWatcher() = default;
    ~DreamWatcher() {
        Stop();
    }

    static FILETIME GetFileWriteTime(const std::string& path) {
        FILETIME ft{0, 0};
        WIN32_FILE_ATTRIBUTE_DATA data;
        if (GetFileAttributesExA(path.c_str(), GetFileExInfoStandard, &data)) {
            ft = data.ftLastWriteTime;
        }
        return ft;
    }

    std::atomic<bool> m_running{false};
    std::thread m_workerThread;
    std::mutex m_mutex;
    std::unordered_map<std::string, WatchedFile> m_watchedFiles;
    std::unordered_map<std::string, ShadowModuleInfo> m_shadowModules;
};

} // namespace dreamkurumi
