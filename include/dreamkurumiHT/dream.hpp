#pragma once

// ============================================================================
//  _____                               ____             
// |  __ \                             / ___|            
// | |  | |_ __ ___  __ _ _ __ ___    | |     ___  _ __ ___ 
// | |  | | '__/ _ \/ _` | '_ ` _ \   | |    / _ \| '__/ _ \
// | |__| | | |  __/ (_| | | | | | |  | |___| (_) | | |  __/
// |_____/|_|  \___|\__,_|_| |_| |_|   \____|\___/|_|  \___|
//
// dream.hpp - The Unified Single-Header C++ Modding SDK for Enshrouded
// Powered by dreamkurumiHT Native Harmony Engine & Universal Mod Loader
// Zero-dependency, single-include drop-in for fast, safe, modern C++ mods!
// ============================================================================

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>
#include <functional>
#include <memory>
#include <sstream>
#include <type_traits>
#include <cstdio>
#include <cstring>
#include <dreamkurumiHT/dreamkurumiHT.hpp>

// ----------------------------------------------------------------------------
// XHL Native Plugin Host ABI Definitions
// ----------------------------------------------------------------------------
#pragma pack(push, 1)
struct XhlPluginQueryInfo {
    uint32_t structSize;    // 0x00 (0xEC = 236 bytes)
    uint32_t abiVersion;    // 0x04 (0x10000 = 65536)
    uint32_t reserved;      // 0x08
    char id[64];            // 0x0C
    char name[128];         // 0x4C
    char version[32];       // 0xCC
};
#pragma pack(pop)

struct XhlPluginContext {
    uint32_t structSize;                          // 0x00 (40 bytes / 0x28)
    uint32_t apiVersion;                          // 0x04 (0x10000)
    HMODULE gameModule;                           // 0x08
    uint64_t reserved;                            // 0x10
    uint32_t processId;                           // 0x18
    uint32_t padding;                             // 0x1C
    void (*logCallback)(const char* utf8Message); // 0x20
};

// ----------------------------------------------------------------------------
// Core Hook Priority
// ----------------------------------------------------------------------------
namespace dream {
    enum class Priority : int {
        First   = 0,
        Highest = 100,
        High    = 200,
        Normal  = 300,
        Low     = 400,
        Lowest  = 500,
        Last    = 600
    };
}

// ----------------------------------------------------------------------------
// Dynamic Engine Host Dynamic Linker
// ----------------------------------------------------------------------------
namespace dream::detail {

class EngineBridge {
public:
    static EngineBridge& Instance() {
        static EngineBridge s_instance;
        return s_instance;
    }

    HMODULE GetCoreModule() {
        if (!m_hModule) {
            m_hModule = GetModuleHandleW(L"dreamkurumiHT.dll");
            if (!m_hModule) m_hModule = GetModuleHandleW(L"mods\\dreamkurumiHT\\dreamkurumiHT.dll");
            if (!m_hModule) m_hModule = LoadLibraryW(L"mods\\dreamkurumiHT\\dreamkurumiHT.dll");
            if (!m_hModule) m_hModule = LoadLibraryW(L"dreamkurumiHT.dll");
        }
        return m_hModule;
    }

    template <typename T>
    T GetProc(const char* procName) {
        HMODULE hMod = GetCoreModule();
        if (!hMod) return nullptr;
        return reinterpret_cast<T>(GetProcAddress(hMod, procName));
    }

    void (*LogCallback)(const char*) = nullptr;

private:
    HMODULE m_hModule{nullptr};
};

} // namespace dream::detail

// ----------------------------------------------------------------------------
// High-Level Modding API (dream::)
// ----------------------------------------------------------------------------
namespace dream {

// 1. In-Game Visual Toast Notification (HUD OSD)
inline void ShowToast(const std::string& message, float durationSec = 2.0f, uint32_t colorRgb = 0x00FF88) {
    using FnToast = void (*)(const char*, float, uint32_t);
    static auto pfn = detail::EngineBridge::Instance().GetProc<FnToast>("DK_ShowToast");
    if (pfn) {
        pfn(message.c_str(), durationSec, colorRgb);
    }
}

// 2. Logging
inline void Log(const char* fmt, ...) {
    char buf[1024];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    if (detail::EngineBridge::Instance().LogCallback) {
        detail::EngineBridge::Instance().LogCallback(buf);
    } else {
        OutputDebugStringA(buf);
        OutputDebugStringA("\n");
    }
}

// 3. Memory & Pattern Scanning
namespace Memory {
    inline void* Scan(const std::string& pattern) {
        using FnResolve = void* (*)(const char*, const char*, uintptr_t);
        static auto pfn = detail::EngineBridge::Instance().GetProc<FnResolve>("DK_ResolvePattern");
        if (pfn) return pfn("Pattern", pattern.c_str(), 0);
        return nullptr;
    }

    inline void* Resolve(const std::string& symbolName, uintptr_t fallbackRva = 0) {
        using FnResolve = void* (*)(const char*, uintptr_t);
        static auto pfn = detail::EngineBridge::Instance().GetProc<FnResolve>("DK_Resolve");
        if (pfn) return pfn(symbolName.c_str(), fallbackRva);
        return nullptr;
    }

    template <typename T>
    inline T Read(uintptr_t address, const T& defaultVal = T{}) {
        if (!address) return defaultVal;
        T val{};
        SIZE_T read = 0;
        if (ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<LPCVOID>(address), &val, sizeof(T), &read) && read == sizeof(T)) {
            return val;
        }
        return defaultVal;
    }

    template <typename T>
    inline bool Write(uintptr_t address, const T& val) {
        if (!address) return false;
        DWORD oldProt = 0;
        if (VirtualProtect(reinterpret_cast<LPVOID>(address), sizeof(T), PAGE_EXECUTE_READWRITE, &oldProt)) {
            *reinterpret_cast<T*>(address) = val;
            VirtualProtect(reinterpret_cast<LPVOID>(address), sizeof(T), oldProt, &oldProt);
            FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<LPCVOID>(address), sizeof(T));
            return true;
        }
        return false;
    }

    inline bool Patch(uintptr_t address, const std::vector<uint8_t>& bytes) {
        if (!address || bytes.empty()) return false;
        DWORD oldProt = 0;
        if (VirtualProtect(reinterpret_cast<LPVOID>(address), bytes.size(), PAGE_EXECUTE_READWRITE, &oldProt)) {
            std::memcpy(reinterpret_cast<void*>(address), bytes.data(), bytes.size());
            VirtualProtect(reinterpret_cast<LPVOID>(address), bytes.size(), oldProt, &oldProt);
            FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<LPCVOID>(address), bytes.size());
            return true;
        }
        return false;
    }
}

// 4. Configuration (Bindable INI/TOML)
namespace Config {
    template <typename T>
    inline T Get(const std::string& key, const T& defaultVal = T{}) {
        if constexpr (std::is_same_v<T, bool>) {
            using Fn = bool (*)(const char*, bool);
            static auto pfn = detail::EngineBridge::Instance().GetProc<Fn>("DK_Config_GetBool");
            if (pfn) return pfn(key.c_str(), defaultVal);
            return ::dreamkurumi::DreamLiveConfig::Instance().GetBool(key, defaultVal);
        } else if constexpr (std::is_integral_v<T>) {
            using Fn = int (*)(const char*, int);
            static auto pfn = detail::EngineBridge::Instance().GetProc<Fn>("DK_Config_GetInt");
            if (pfn) return static_cast<T>(pfn(key.c_str(), static_cast<int>(defaultVal)));
            return static_cast<T>(::dreamkurumi::DreamLiveConfig::Instance().GetInt(key, static_cast<int>(defaultVal)));
        } else if constexpr (std::is_floating_point_v<T>) {
            using Fn = float (*)(const char*, float);
            static auto pfn = detail::EngineBridge::Instance().GetProc<Fn>("DK_Config_GetFloat");
            if (pfn) return static_cast<T>(pfn(key.c_str(), static_cast<float>(defaultVal)));
            return static_cast<T>(::dreamkurumi::DreamLiveConfig::Instance().GetFloat(key, static_cast<float>(defaultVal)));
        } else if constexpr (std::is_same_v<T, std::string>) {
            using Fn = bool (*)(const char*, char*, uint32_t, const char*);
            static auto pfn = detail::EngineBridge::Instance().GetProc<Fn>("DK_Config_GetString");
            if (pfn) {
                char buf[512] = {0};
                if (pfn(key.c_str(), buf, sizeof(buf), defaultVal.c_str())) {
                    return std::string(buf);
                }
            }
            std::string directVal = ::dreamkurumi::DreamLiveConfig::Instance().Get(key);
            if (!directVal.empty()) return directVal;
            return defaultVal;
        }
        return defaultVal;
    }
}

// 5. Anti-Crash Sandbox
inline bool ExecuteSafely(const char* taskName, void (*callback)(void*), void* userData = nullptr) {
    using FnExec = bool (*)(const char*, void (*)(void*), void*);
    static auto pfn = detail::EngineBridge::Instance().GetProc<FnExec>("DK_ExecuteProtected");
    if (pfn) return pfn(taskName, callback, userData);
    if (callback) callback(userData);
    return true;
}

} // namespace dream

// ----------------------------------------------------------------------------
// Declarative Harmony Patch Macros
// ----------------------------------------------------------------------------

#define DREAM_PATCH(PatchClass, SymbolNameStr, RetType, ...) \
    DREAM_HARMONY_PATCH(PatchClass, SymbolNameStr, 0, RetType, __VA_ARGS__)

#define DREAM_PATCH_PRIORITY(PatchClass, SymbolNameStr, PriorityVal, RetType, ...) \
    DREAM_HARMONY_PATCH_PRIORITY(PatchClass, SymbolNameStr, PriorityVal, 0, RetType, __VA_ARGS__)

#define DREAM_PATCH_AOB(PatchClass, SymbolNameStr, PrimaryPatternStr, FallbackPatternStr, RetType, ...) \
    DREAM_HARMONY_AOB_RESILIENT(PatchClass, SymbolNameStr, PrimaryPatternStr, FallbackPatternStr, 0, RetType, __VA_ARGS__)

#define DREAM_PATCH_AOB_PRIORITY(PatchClass, SymbolNameStr, PrimaryPatternStr, FallbackPatternStr, PriorityVal, RetType, ...) \
    DREAM_HARMONY_AOB_RESILIENT_PRIORITY(PatchClass, SymbolNameStr, PrimaryPatternStr, FallbackPatternStr, PriorityVal, 0, RetType, __VA_ARGS__)

// ----------------------------------------------------------------------------
// Universal Plugin Lifecycle Generator Macro
// ----------------------------------------------------------------------------
#define DREAM_PLUGIN(PluginIdStr, PluginNameStr, VersionStr, AuthorStr) \
    extern "C" { \
        __declspec(dllexport) int XhlNativePluginQuery(XhlPluginQueryInfo* info) { \
            if (!info) return 0; \
            std::memset(info, 0, sizeof(XhlPluginQueryInfo)); \
            info->structSize = sizeof(XhlPluginQueryInfo); \
            info->abiVersion = 0x10000; \
            strncpy(info->id, PluginIdStr, sizeof(info->id) - 1); \
            strncpy(info->name, PluginNameStr, sizeof(info->name) - 1); \
            strncpy(info->version, VersionStr, sizeof(info->version) - 1); \
            return 1; \
        } \
        __declspec(dllexport) int XhlNativePluginStart(const XhlPluginContext* ctx) { \
            if (ctx && ctx->logCallback) { \
                ::dream::detail::EngineBridge::Instance().LogCallback = ctx->logCallback; \
            } \
            ::dreamkurumi::DreamHarmony::PatchAll(); \
            ::dream::Log("[DreamPlugin] Started: %s v%s by %s", PluginNameStr, VersionStr, AuthorStr); \
            return 1; \
        } \
        __declspec(dllexport) void XhlNativePluginRequestStop() { \
            ::dreamkurumi::DreamHarmony::UnpatchAll(); \
            ::dream::Log("[DreamPlugin] Stopped: %s", PluginNameStr); \
        } \
    } \
    BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpReserved) { \
        if (fdwReason == DLL_PROCESS_ATTACH) { \
            DisableThreadLibraryCalls(hinstDLL); \
        } else if (fdwReason == DLL_PROCESS_DETACH && !lpReserved) { \
            ::dreamkurumi::DreamHarmony::UnpatchAll(); \
        } \
        return TRUE; \
    }
