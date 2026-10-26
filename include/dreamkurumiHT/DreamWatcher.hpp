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
            m_workerThread.detach();
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

private:
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
};

} // namespace dreamkurumi
