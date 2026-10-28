#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <string>
#include <vector>
#include <atomic>
#include <mutex>
#include <algorithm>
#include "DreamLog.hpp"
#include "DreamCrashGuard.hpp"

namespace dreamkurumi {

// ============================================================================
// DreamChainLoader - Multi-DLL Proxy Chain-Loading Architecture
// Enables seamless coexistence of multiple DLL proxies (dinput8, dbghelp, etc.)
// ============================================================================
class DreamChainLoader {
public:
    struct ChainedModule {
        std::string path;
        HMODULE hModule{nullptr};
        bool isInitialized{false};
    };

    static DreamChainLoader& Instance() {
        static DreamChainLoader s_instance;
        return s_instance;
    }

    // Load a specific secondary proxy DLL and register into chain
    bool ChainLoadDll(const std::string& dllPath) {
        std::lock_guard<std::mutex> lock(m_mutex);

        // Check if already loaded
        for (const auto& mod : m_modules) {
            if (_stricmp(mod.path.c_str(), dllPath.c_str()) == 0) {
                DREAM_LOG_INFO("DreamChainLoader: DLL '%s' already chained", dllPath.c_str());
                return true;
            }
        }

        // Verify file exists
        DWORD attrs = GetFileAttributesA(dllPath.c_str());
        if (attrs == INVALID_FILE_ATTRIBUTES || (attrs & FILE_ATTRIBUTE_DIRECTORY)) {
            DREAM_LOG_DEBUG("DreamChainLoader: Chained target '%s' not present on disk", dllPath.c_str());
            return false;
        }

        // Load the chained DLL safely
        HMODULE hMod = LoadLibraryExA(dllPath.c_str(), NULL, LOAD_WITH_ALTERED_SEARCH_PATH);
        if (!hMod) {
            DWORD err = GetLastError();
            DREAM_LOG_ERROR("DreamChainLoader: Failed to load chained DLL '%s' (Error: %lu)", dllPath.c_str(), err);
            return false;
        }

        ChainedModule chained;
        chained.path = dllPath;
        chained.hModule = hMod;
        chained.isInitialized = true;
        m_modules.push_back(chained);

        DREAM_LOG_INFO("DreamChainLoader: Successfully chained '%s' at %p", dllPath.c_str(), hMod);
        return true;
    }

    // Scan standard secondary chain locations and chain_loader.ini
    size_t ScanAndLoadChainedModules(const std::string& iniConfigPath = "chain_loader.ini") {
        size_t loadedCount = 0;

        // 1. Check well-known secondary proxy names
        const char* defaultChains[] = {
            "dinput8_chain.dll",
            "dinput8_orig.dll",
            "dbghelp_chain.dll",
            "mods\\chain\\*.dll"
        };

        for (const char* target : defaultChains) {
            if (strstr(target, "*") == nullptr) {
                if (ChainLoadDll(target)) {
                    loadedCount++;
                }
            }
        }

        // 2. Scan directory mods\chain\ if present
        WIN32_FIND_DATAA fd;
        HANDLE hFind = FindFirstFileA("mods\\chain\\*.dll", &fd);
        if (hFind != INVALID_HANDLE_VALUE) {
            do {
                if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
                    std::string fullPath = std::string("mods\\chain\\") + fd.cFileName;
                    if (ChainLoadDll(fullPath)) {
                        loadedCount++;
                    }
                }
            } while (FindNextFileA(hFind, &fd));
            FindClose(hFind);
        }

        // 3. Read custom modules from INI if present
        if (GetFileAttributesA(iniConfigPath.c_str()) != INVALID_FILE_ATTRIBUTES) {
            char buffer[2048] = {0};
            DWORD len = GetPrivateProfileStringA("ChainLoader", "Modules", "", buffer, sizeof(buffer), iniConfigPath.c_str());
            if (len > 0) {
                char* context = nullptr;
                char* token = strtok_s(buffer, ",;", &context);
                while (token) {
                    // Trim whitespace
                    std::string modPath = token;
                    modPath.erase(0, modPath.find_first_not_of(" \t\r\n"));
                    modPath.erase(modPath.find_last_not_of(" \t\r\n") + 1);

                    if (!modPath.empty() && ChainLoadDll(modPath)) {
                        loadedCount++;
                    }
                    token = strtok_s(nullptr, ",;", &context);
                }
            }
        }

        DREAM_LOG_INFO("DreamChainLoader: Total chained modules active: %zu", m_modules.size());
        return loadedCount;
    }

    // Resolve procedure address from chained modules
    void* GetChainedProcAddress(const char* procName) {
        std::lock_guard<std::mutex> lock(m_mutex);
        for (const auto& mod : m_modules) {
            if (mod.hModule) {
                void* pfn = reinterpret_cast<void*>(GetProcAddress(mod.hModule, procName));
                if (pfn) return pfn;
            }
        }
        return nullptr;
    }

    // Get count of active chained modules
    size_t GetChainedCount() const {
        return m_modules.size();
    }

    // Unload all chained modules cleanly
    void UnloadAll() {
        std::lock_guard<std::mutex> lock(m_mutex);
        for (auto& mod : m_modules) {
            if (mod.hModule) {
                FreeLibrary(mod.hModule);
                mod.hModule = nullptr;
                mod.isInitialized = false;
            }
        }
        m_modules.clear();
        DREAM_LOG_INFO("DreamChainLoader: All chained modules unloaded cleanly");
    }

private:
    DreamChainLoader() = default;
    ~DreamChainLoader() {
        UnloadAll();
    }

    std::mutex m_mutex;
    std::vector<ChainedModule> m_modules;
};

} // namespace dreamkurumi
