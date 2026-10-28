#pragma once
#include <windows.h>
#include <cstdint>
#include <cstddef>
#include <cstdio>
#include <string>
#include <vector>
#include "DreamEnshroudedTypes.hpp"

// ============================================================================
// dreamkurumiHT Harmony Plugin Interface (Lightweight C-ABI Dynamic Link)
// Designed for ultra-light mod DLLs (~30-80 KB) linked to dreamkurumiHT.dll
// ============================================================================

namespace dreamkurumi {

// ============================================================================
// Extended SIMD / AVX2 CPU Register Context for Plugins (v3.8.0-beta)
// ============================================================================
#pragma pack(push, 1)
struct DreamYmmRegister {
    union {
        float    f32[8];
        double   f64[4];
        uint32_t u32[8];
        uint64_t u64[4];
        uint8_t  u8[32];
    };
};

struct DreamCpuContext {
    union {
        DreamYmmRegister ymm[16];   // 0x000 - 0x1FF (512 bytes, full 256-bit AVX/AVX2)
        struct {
            union {
                float    f32[4];
                double   f64[2];
                uint32_t u32[4];
                uint64_t u64[2];
                uint8_t  u8[16];
            };
            uint8_t _pad_high[16];  // upper 128 bits of 256-bit YMM slot
        } xmm[16];                  // 0x000 - 0x1FF (overlays with ymm[16] for 100% backward compatibility)
    };

    uint64_t r15;       // 0x200
    uint64_t r14;       // 0x208
    uint64_t r13;       // 0x210
    uint64_t r12;       // 0x218
    uint64_t r11;       // 0x220
    uint64_t r10;       // 0x228
    uint64_t r9;        // 0x230
    uint64_t r8;        // 0x238
    uint64_t rbp;       // 0x240
    uint64_t rdi;       // 0x248
    uint64_t rsi;       // 0x250
    uint64_t rdx;       // 0x258
    uint64_t rcx;       // 0x260
    uint64_t rbx;       // 0x268
    uint64_t rax;       // 0x270
    uint64_t rflags;    // 0x278

    float GetFloatArg(int index) const {
        if (index >= 0 && index < 16) return xmm[index].f32[0];
        return 0.0f;
    }
    void SetFloatArg(int index, float val) {
        if (index >= 0 && index < 16) xmm[index].f32[0] = val;
    }
};
#pragma pack(pop)


// Function pointer signatures for all core dreamkurumiHT exports
typedef bool (*FnDK_Initialize)(const char* cachePath);
typedef size_t (*FnDK_PatchAll)();
typedef void (*FnDK_UnpatchAll)();
typedef void* (*FnDK_Resolve)(const char* symbolName, uintptr_t fallbackRva);
typedef void* (*FnDK_ResolvePattern)(const char* symbolName, const char* pattern, uintptr_t fallbackRva);
typedef uintptr_t (*FnDK_GetModuleBase)();
typedef uint32_t (*FnDK_GetGameBuildTimestamp)();

typedef void* (*FnDK_InlineHook)(void* target, void* detour, void** ppOriginal);
typedef bool (*FnDK_Unhook)(void* hookHandle);
typedef void* (*FnDK_MidHook)(void* target, void (*cb)(void* ctx));
typedef bool (*FnDK_MidUnhook)(void* hookHandle);
typedef bool (*FnDK_RegisterChainedMidHook)(void* target, void (*cb)(void* ctx), int priority, const char* name);
typedef void* (*FnDK_PatchBytes)(void* target, const uint8_t* bytes, size_t count);
typedef void* (*FnDK_Nop)(void* target, size_t count);
typedef bool (*FnDK_RestorePatch)(void* patchHandle);

typedef bool (*FnDK_ExecuteProtected)(const char* modName, void (*cb)(void* userData), void* userData);
typedef void* (*FnDK_GetEntity)(void* pContext, uint32_t entityId);
typedef void* (*FnDK_GetEntityComponent)(void* pEntity, uint16_t compId);
typedef bool (*FnDK_GetEntityPosition)(void* pEntity, float* outX, float* outY, float* outZ);

typedef void (*FnDK_Log)(int level, const char* msg);

typedef void (*FnDK_ShowToast)(const char* message, float durationSec, uint32_t colorRgb);

typedef bool (*FnDK_Vulkan_ConfigureLayerEnvironment)(const char* layerDirPath, const char* layerName);
typedef bool (*FnDK_Vulkan_GenerateLayerManifest)(const char* jsonPath, const char* layerName, const char* dllPath, const char* description);
typedef bool (*FnDK_ChainLoader_LoadChainedDll)(const char* dllPath);
typedef size_t (*FnDK_ChainLoader_ScanAndLoad)(const char* iniPath);
typedef size_t (*FnDK_ChainLoader_GetCount)();
typedef bool (*FnDK_Asset_RegisterOverride)(const char* resourceType, const char* resourceName, const void* data, size_t size, const char* originMod);
typedef bool (*FnDK_Asset_GetOverride)(const char* resourceType, const char* resourceName, const void** outData, size_t* outSize);
typedef bool (*FnDK_Asset_RegisterLocaTag)(const char* tag, const char* utf8Text);
typedef void (*FnDK_Asset_ClearAll)();

typedef uint64_t (*FnDK_Scheduler_Enqueue)(void (*fn)(void*), void* userData);
typedef uint64_t (*FnDK_Scheduler_RunAfter)(uint32_t delayMs, void (*fn)(void*), void* userData);
typedef bool (*FnDK_Scheduler_Cancel)(uint64_t taskId);
typedef size_t (*FnDK_Scheduler_Tick)(double maxTimeBudgetMs);
typedef bool (*FnDK_CpuSupportsAvx)();


typedef void (*FnDK_Preset_SetGodMode)(bool enable);
typedef void (*FnDK_Preset_SetGhostMode)(bool enable);
typedef void (*FnDK_Preset_SetInfiniteStamina)(bool enable);
typedef void (*FnDK_Preset_SetInfiniteMana)(bool enable);
typedef void (*FnDK_Preset_SetInfiniteShroud)(bool enable);
typedef void (*FnDK_Preset_SetFreeCraft)(bool enable);
typedef void (*FnDK_Preset_SetSuperMining)(bool enable, float mult);
typedef void (*FnDK_Preset_SetUnlimitedFlight)(bool enable);
typedef void (*FnDK_Preset_SetInfiniteItemSplit)(bool enable);
typedef void (*FnDK_Preset_SetInfiniteItemUse)(bool enable);
typedef void (*FnDK_Preset_SetNoResourceCost)(bool enable);
typedef void (*FnDK_Preset_SetInfiniteDurability)(bool enable);
typedef void (*FnDK_Preset_SetOneShotKill)(bool enable);
typedef void (*FnDK_Preset_SetFastProduction)(bool enable, float seconds);
typedef bool (*FnDK_Preset_SaveConfig)(const char* path);
typedef bool (*FnDK_Preset_LoadConfig)(const char* path);
typedef bool (*FnDK_Preset_IsDedicatedServer)();

typedef void (*FnDK_Overlay_ToggleMenu)();
typedef bool (*FnDK_Overlay_IsMenuOpen)();
typedef void (*FnDK_Overlay_SetMenuKey)(int vKey);
typedef bool (*FnDK_Overlay_ExecuteCommand)(const char* cmd);

typedef bool (*FnDK_Env_IsDedicatedServer)();
typedef bool (*FnDK_Env_IsClient)();

typedef void (*FnDK_Watchdog_Heartbeat)();
typedef void (*FnDK_Watchdog_RegisterThread)(const char* name);
typedef void (*FnDK_Profiler_PrintReport)();
typedef void (*FnDK_Profiler_Reset)();
typedef void (*FnDK_Profiler_SetEnabled)(bool enable);

typedef void (*FnDK_Player_OnLogin)(uint32_t entityId, const char* name, bool isAdmin);
typedef void (*FnDK_Player_OnDisconnect)(uint32_t entityId);
typedef uint32_t (*FnDK_Player_GetCount)();
typedef void (*FnDK_Player_Broadcast)(const char* message);
typedef bool (*FnDK_Player_Kick)(uint32_t entityId, const char* reason);
typedef bool (*FnDK_Backup_Create)(const char* tag);
typedef bool (*FnDK_Config_Reload)();

typedef void (*FnDK_EventCallback)(const char* eventName, const void* data, size_t size, void* userData);
typedef uint64_t (*FnDK_Event_Subscribe)(const char* eventName, FnDK_EventCallback cb, void* userData);
typedef bool (*FnDK_Event_Unsubscribe)(uint64_t subId);
typedef size_t (*FnDK_Event_Publish)(const char* eventName, const void* data, size_t size);

typedef void (*FnDK_Config_BindFloat)(const char* section, const char* key, float defaultVal, const char* desc);
typedef void (*FnDK_Config_BindInt)(const char* section, const char* key, int defaultVal, const char* desc);
typedef void (*FnDK_Config_BindBool)(const char* section, const char* key, bool defaultVal, const char* desc);
typedef void (*FnDK_Config_BindString)(const char* section, const char* key, const char* defaultVal, const char* desc);
typedef float (*FnDK_Config_GetFloat)(const char* fullKey, float fallback);
typedef void (*FnDK_Config_SetFloat)(const char* fullKey, float val);
typedef int (*FnDK_Config_GetInt)(const char* fullKey, int fallback);
typedef void (*FnDK_Config_SetInt)(const char* fullKey, int val);
typedef bool (*FnDK_Config_GetBool)(const char* fullKey, bool fallback);
typedef void (*FnDK_Config_SetBool)(const char* fullKey, bool val);
typedef bool (*FnDK_Config_GetString)(const char* fullKey, char* outBuffer, uint32_t outSize, const char* fallback);
typedef void (*FnDK_Config_SetString)(const char* fullKey, const char* val);

class PluginHost {
public:
    static PluginHost& Instance() {
        static PluginHost inst;
        return inst;
    }

    bool Init() {
        if (m_initialized) return true;

        m_hMod = GetModuleHandleA("dreamkurumiHT.dll");
        if (!m_hMod) m_hMod = LoadLibraryA("dreamkurumiHT.dll");
        if (!m_hMod) m_hMod = LoadLibraryA("mods\\dreamkurumiHT\\dreamkurumiHT.dll");
        if (!m_hMod) m_hMod = LoadLibraryA("..\\dreamkurumiHT\\dreamkurumiHT.dll");
        if (!m_hMod) return false;

        #define BIND_DK(name) pfn_##name = (Fn##name)GetProcAddress(m_hMod, #name)
        BIND_DK(DK_Initialize);
        BIND_DK(DK_PatchAll);
        BIND_DK(DK_UnpatchAll);
        BIND_DK(DK_Resolve);
        BIND_DK(DK_ResolvePattern);
        BIND_DK(DK_GetModuleBase);
        BIND_DK(DK_GetGameBuildTimestamp);

        BIND_DK(DK_InlineHook);
        BIND_DK(DK_Unhook);
        BIND_DK(DK_MidHook);
        BIND_DK(DK_MidUnhook);
        BIND_DK(DK_RegisterChainedMidHook);
        BIND_DK(DK_PatchBytes);
        BIND_DK(DK_Nop);
        BIND_DK(DK_RestorePatch);
        BIND_DK(DK_ExecuteProtected);
        BIND_DK(DK_GetEntity);
        BIND_DK(DK_GetEntityComponent);
        BIND_DK(DK_GetEntityPosition);

        BIND_DK(DK_Log);

        BIND_DK(DK_ShowToast);
        BIND_DK(DK_Vulkan_ConfigureLayerEnvironment);
        BIND_DK(DK_Vulkan_GenerateLayerManifest);
        BIND_DK(DK_ChainLoader_LoadChainedDll);
        BIND_DK(DK_ChainLoader_ScanAndLoad);
        BIND_DK(DK_ChainLoader_GetCount);
        BIND_DK(DK_Asset_RegisterOverride);
        BIND_DK(DK_Asset_GetOverride);
        BIND_DK(DK_Asset_RegisterLocaTag);
        BIND_DK(DK_Asset_ClearAll);
        BIND_DK(DK_Scheduler_Enqueue);
        BIND_DK(DK_Scheduler_RunAfter);
        BIND_DK(DK_Scheduler_Cancel);
        BIND_DK(DK_Scheduler_Tick);
        BIND_DK(DK_CpuSupportsAvx);


        BIND_DK(DK_Preset_SetGodMode);
        BIND_DK(DK_Preset_SetGhostMode);
        BIND_DK(DK_Preset_SetInfiniteStamina);
        BIND_DK(DK_Preset_SetInfiniteMana);
        BIND_DK(DK_Preset_SetInfiniteShroud);
        BIND_DK(DK_Preset_SetFreeCraft);
        BIND_DK(DK_Preset_SetSuperMining);
        BIND_DK(DK_Preset_SetUnlimitedFlight);
        BIND_DK(DK_Preset_SetInfiniteItemSplit);
        BIND_DK(DK_Preset_SetInfiniteItemUse);
        BIND_DK(DK_Preset_SetNoResourceCost);
        BIND_DK(DK_Preset_SetInfiniteDurability);
        BIND_DK(DK_Preset_SetOneShotKill);
        BIND_DK(DK_Preset_SetFastProduction);
        BIND_DK(DK_Preset_SaveConfig);
        BIND_DK(DK_Preset_LoadConfig);
        BIND_DK(DK_Preset_IsDedicatedServer);

        BIND_DK(DK_Overlay_ToggleMenu);
        BIND_DK(DK_Overlay_IsMenuOpen);
        BIND_DK(DK_Overlay_SetMenuKey);
        BIND_DK(DK_Overlay_ExecuteCommand);

        BIND_DK(DK_Env_IsDedicatedServer);
        BIND_DK(DK_Env_IsClient);

        BIND_DK(DK_Watchdog_Heartbeat);
        BIND_DK(DK_Watchdog_RegisterThread);
        BIND_DK(DK_Profiler_PrintReport);
        BIND_DK(DK_Profiler_Reset);
        BIND_DK(DK_Profiler_SetEnabled);

        BIND_DK(DK_Player_OnLogin);
        BIND_DK(DK_Player_OnDisconnect);
        BIND_DK(DK_Player_GetCount);
        BIND_DK(DK_Player_Broadcast);
        BIND_DK(DK_Player_Kick);
        BIND_DK(DK_Backup_Create);
        BIND_DK(DK_Config_Reload);

        BIND_DK(DK_Event_Subscribe);
        BIND_DK(DK_Event_Unsubscribe);
        BIND_DK(DK_Event_Publish);

        BIND_DK(DK_Config_BindFloat);
        BIND_DK(DK_Config_BindInt);
        BIND_DK(DK_Config_BindBool);
        BIND_DK(DK_Config_BindString);
        BIND_DK(DK_Config_GetFloat);
        BIND_DK(DK_Config_SetFloat);
        BIND_DK(DK_Config_GetInt);
        BIND_DK(DK_Config_SetInt);
        BIND_DK(DK_Config_GetBool);
        BIND_DK(DK_Config_SetBool);
        BIND_DK(DK_Config_GetString);
        BIND_DK(DK_Config_SetString);
        #undef BIND_DK

        m_initialized = true;
        return true;
    }

    HMODULE m_hMod = nullptr;
    bool m_initialized = false;

    FnDK_Initialize pfn_DK_Initialize = nullptr;
    FnDK_PatchAll pfn_DK_PatchAll = nullptr;
    FnDK_UnpatchAll pfn_DK_UnpatchAll = nullptr;
    FnDK_Resolve pfn_DK_Resolve = nullptr;
    FnDK_ResolvePattern pfn_DK_ResolvePattern = nullptr;
    FnDK_GetModuleBase pfn_DK_GetModuleBase = nullptr;
    FnDK_GetGameBuildTimestamp pfn_DK_GetGameBuildTimestamp = nullptr;

    FnDK_InlineHook pfn_DK_InlineHook = nullptr;
    FnDK_Unhook pfn_DK_Unhook = nullptr;
    FnDK_MidHook pfn_DK_MidHook = nullptr;
    FnDK_MidUnhook pfn_DK_MidUnhook = nullptr;
    FnDK_RegisterChainedMidHook pfn_DK_RegisterChainedMidHook = nullptr;
    FnDK_PatchBytes pfn_DK_PatchBytes = nullptr;
    FnDK_Nop pfn_DK_Nop = nullptr;
    FnDK_RestorePatch pfn_DK_RestorePatch = nullptr;
    FnDK_ExecuteProtected pfn_DK_ExecuteProtected = nullptr;
    FnDK_GetEntity pfn_DK_GetEntity = nullptr;
    FnDK_GetEntityComponent pfn_DK_GetEntityComponent = nullptr;
    FnDK_GetEntityPosition pfn_DK_GetEntityPosition = nullptr;

    FnDK_Log pfn_DK_Log = nullptr;

    FnDK_ShowToast pfn_DK_ShowToast = nullptr;
    FnDK_Vulkan_ConfigureLayerEnvironment pfn_DK_Vulkan_ConfigureLayerEnvironment = nullptr;
    FnDK_Vulkan_GenerateLayerManifest pfn_DK_Vulkan_GenerateLayerManifest = nullptr;
    FnDK_ChainLoader_LoadChainedDll pfn_DK_ChainLoader_LoadChainedDll = nullptr;
    FnDK_ChainLoader_ScanAndLoad pfn_DK_ChainLoader_ScanAndLoad = nullptr;
    FnDK_ChainLoader_GetCount pfn_DK_ChainLoader_GetCount = nullptr;
    FnDK_Asset_RegisterOverride pfn_DK_Asset_RegisterOverride = nullptr;
    FnDK_Asset_GetOverride pfn_DK_Asset_GetOverride = nullptr;
    FnDK_Asset_RegisterLocaTag pfn_DK_Asset_RegisterLocaTag = nullptr;
    FnDK_Asset_ClearAll pfn_DK_Asset_ClearAll = nullptr;

    FnDK_Scheduler_Enqueue pfn_DK_Scheduler_Enqueue = nullptr;
    FnDK_Scheduler_RunAfter pfn_DK_Scheduler_RunAfter = nullptr;
    FnDK_Scheduler_Cancel pfn_DK_Scheduler_Cancel = nullptr;
    FnDK_Scheduler_Tick pfn_DK_Scheduler_Tick = nullptr;
    FnDK_CpuSupportsAvx pfn_DK_CpuSupportsAvx = nullptr;


    FnDK_Preset_SetGodMode pfn_DK_Preset_SetGodMode = nullptr;
    FnDK_Preset_SetGhostMode pfn_DK_Preset_SetGhostMode = nullptr;
    FnDK_Preset_SetInfiniteStamina pfn_DK_Preset_SetInfiniteStamina = nullptr;
    FnDK_Preset_SetInfiniteMana pfn_DK_Preset_SetInfiniteMana = nullptr;
    FnDK_Preset_SetInfiniteShroud pfn_DK_Preset_SetInfiniteShroud = nullptr;
    FnDK_Preset_SetFreeCraft pfn_DK_Preset_SetFreeCraft = nullptr;
    FnDK_Preset_SetSuperMining pfn_DK_Preset_SetSuperMining = nullptr;
    FnDK_Preset_SetUnlimitedFlight pfn_DK_Preset_SetUnlimitedFlight = nullptr;
    FnDK_Preset_SetInfiniteItemSplit pfn_DK_Preset_SetInfiniteItemSplit = nullptr;
    FnDK_Preset_SetInfiniteItemUse pfn_DK_Preset_SetInfiniteItemUse = nullptr;
    FnDK_Preset_SetNoResourceCost pfn_DK_Preset_SetNoResourceCost = nullptr;
    FnDK_Preset_SetInfiniteDurability pfn_DK_Preset_SetInfiniteDurability = nullptr;
    FnDK_Preset_SetOneShotKill pfn_DK_Preset_SetOneShotKill = nullptr;
    FnDK_Preset_SetFastProduction pfn_DK_Preset_SetFastProduction = nullptr;
    FnDK_Preset_SaveConfig pfn_DK_Preset_SaveConfig = nullptr;
    FnDK_Preset_LoadConfig pfn_DK_Preset_LoadConfig = nullptr;
    FnDK_Preset_IsDedicatedServer pfn_DK_Preset_IsDedicatedServer = nullptr;

    FnDK_Overlay_ToggleMenu pfn_DK_Overlay_ToggleMenu = nullptr;
    FnDK_Overlay_IsMenuOpen pfn_DK_Overlay_IsMenuOpen = nullptr;
    FnDK_Overlay_SetMenuKey pfn_DK_Overlay_SetMenuKey = nullptr;
    FnDK_Overlay_ExecuteCommand pfn_DK_Overlay_ExecuteCommand = nullptr;

    FnDK_Env_IsDedicatedServer pfn_DK_Env_IsDedicatedServer = nullptr;
    FnDK_Env_IsClient pfn_DK_Env_IsClient = nullptr;

    FnDK_Watchdog_Heartbeat pfn_DK_Watchdog_Heartbeat = nullptr;
    FnDK_Watchdog_RegisterThread pfn_DK_Watchdog_RegisterThread = nullptr;
    FnDK_Profiler_PrintReport pfn_DK_Profiler_PrintReport = nullptr;
    FnDK_Profiler_Reset pfn_DK_Profiler_Reset = nullptr;
    FnDK_Profiler_SetEnabled pfn_DK_Profiler_SetEnabled = nullptr;

    FnDK_Player_OnLogin pfn_DK_Player_OnLogin = nullptr;
    FnDK_Player_OnDisconnect pfn_DK_Player_OnDisconnect = nullptr;
    FnDK_Player_GetCount pfn_DK_Player_GetCount = nullptr;
    FnDK_Player_Broadcast pfn_DK_Player_Broadcast = nullptr;
    FnDK_Player_Kick pfn_DK_Player_Kick = nullptr;
    FnDK_Backup_Create pfn_DK_Backup_Create = nullptr;
    FnDK_Config_Reload pfn_DK_Config_Reload = nullptr;

    FnDK_Event_Subscribe pfn_DK_Event_Subscribe = nullptr;
    FnDK_Event_Unsubscribe pfn_DK_Event_Unsubscribe = nullptr;
    FnDK_Event_Publish pfn_DK_Event_Publish = nullptr;

    FnDK_Config_BindFloat pfn_DK_Config_BindFloat = nullptr;
    FnDK_Config_BindInt pfn_DK_Config_BindInt = nullptr;
    FnDK_Config_BindBool pfn_DK_Config_BindBool = nullptr;
    FnDK_Config_BindString pfn_DK_Config_BindString = nullptr;
    FnDK_Config_GetFloat pfn_DK_Config_GetFloat = nullptr;
    FnDK_Config_SetFloat pfn_DK_Config_SetFloat = nullptr;
    FnDK_Config_GetInt pfn_DK_Config_GetInt = nullptr;
    FnDK_Config_SetInt pfn_DK_Config_SetInt = nullptr;
    FnDK_Config_GetBool pfn_DK_Config_GetBool = nullptr;
    FnDK_Config_SetBool pfn_DK_Config_SetBool = nullptr;
    FnDK_Config_GetString pfn_DK_Config_GetString = nullptr;
    FnDK_Config_SetString pfn_DK_Config_SetString = nullptr;
};

// Clean C++ Helpers
inline void Log(int level, const char* msg) {
    auto& host = PluginHost::Instance();
    if (host.Init() && host.pfn_DK_Log) {
        host.pfn_DK_Log(level, msg);
    }
}



inline void ShowToast(const std::string& message, float durationSec = 2.0f, uint32_t colorRgb = 0x00FF88) {

    auto pfn = PluginHost::Instance().pfn_DK_ShowToast;

    if (pfn) pfn(message.c_str(), durationSec, colorRgb);

}



inline void LogInfo(const char* fmt, ...) {
    char buf[1024];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    Log(1, buf);
}

inline void LogWarn(const char* fmt, ...) {
    char buf[1024];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    Log(2, buf);
}

inline void LogError(const char* fmt, ...) {
    char buf[1024];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    Log(3, buf);
}

inline void SetSuperMining(bool enable, float mult = 1.0f) {
    auto& host = PluginHost::Instance();
    if (host.Init() && host.pfn_DK_Preset_SetSuperMining) {
        host.pfn_DK_Preset_SetSuperMining(enable, mult);
    }
}

inline bool IsDedicatedServer() {
    auto& host = PluginHost::Instance();
    if (host.Init() && host.pfn_DK_Env_IsDedicatedServer) {
        return host.pfn_DK_Env_IsDedicatedServer();
    }
    return false;
}

inline void* ResolvePattern(const char* symbolName, const char* pattern, uintptr_t fallbackRva) {
    auto& host = PluginHost::Instance();
    if (host.Init() && host.pfn_DK_ResolvePattern) {
        return host.pfn_DK_ResolvePattern(symbolName, pattern, fallbackRva);
    }
    return nullptr;
}

inline void* PatchBytes(void* target, const uint8_t* bytes, size_t count) {
    auto& host = PluginHost::Instance();
    if (host.Init() && host.pfn_DK_PatchBytes) {
        return host.pfn_DK_PatchBytes(target, bytes, count);
    }
    return nullptr;
}

inline void* InlineHook(void* target, void* detour, void** ppOriginal) {
    auto& host = PluginHost::Instance();
    if (host.Init() && host.pfn_DK_InlineHook) {
        return host.pfn_DK_InlineHook(target, detour, ppOriginal);
    }
    return nullptr;
}

inline void* MidHook(void* target, void (*cb)(void* ctx)) {
    auto& host = PluginHost::Instance();
    if (host.Init() && host.pfn_DK_MidHook) {
        return host.pfn_DK_MidHook(target, cb);
    }
    return nullptr;
}

inline bool RegisterChainedMidHook(void* target, void (*cb)(void* ctx), int priority = 0, const char* name = "") {
    auto& host = PluginHost::Instance();
    if (host.Init() && host.pfn_DK_RegisterChainedMidHook) {
        return host.pfn_DK_RegisterChainedMidHook(target, cb, priority, name);
    }
    return false;
}

inline bool ExecuteProtected(const char* modName, void (*cb)(void* userData), void* userData) {
    auto& host = PluginHost::Instance();
    if (host.Init() && host.pfn_DK_ExecuteProtected) {
        return host.pfn_DK_ExecuteProtected(modName, cb, userData);
    }
    if (cb) cb(userData);
    return true;
}

inline void* GetEntity(void* pContext, uint32_t entityId) {
    auto& host = PluginHost::Instance();
    if (host.Init() && host.pfn_DK_GetEntity) {
        return host.pfn_DK_GetEntity(pContext, entityId);
    }
    return nullptr;
}

inline void* GetEntityComponent(void* pEntity, uint16_t compId) {
    auto& host = PluginHost::Instance();
    if (host.Init() && host.pfn_DK_GetEntityComponent) {
        return host.pfn_DK_GetEntityComponent(pEntity, compId);
    }
    return nullptr;
}

inline bool GetEntityPosition(void* pEntity, float* outX, float* outY, float* outZ) {
    auto& host = PluginHost::Instance();
    if (host.Init() && host.pfn_DK_GetEntityPosition) {
        return host.pfn_DK_GetEntityPosition(pEntity, outX, outY, outZ);
    }
    return false;
}

inline void NotifyPlayerLogin(uint32_t entityId, const char* name, bool isAdmin) {
    auto& host = PluginHost::Instance();
    if (host.Init() && host.pfn_DK_Player_OnLogin) {
        host.pfn_DK_Player_OnLogin(entityId, name, isAdmin);
    }
}

inline void NotifyPlayerDisconnect(uint32_t entityId) {
    auto& host = PluginHost::Instance();
    if (host.Init() && host.pfn_DK_Player_OnDisconnect) {
        host.pfn_DK_Player_OnDisconnect(entityId);
    }
}

inline uint32_t GetConnectedPlayerCount() {
    auto& host = PluginHost::Instance();
    if (host.Init() && host.pfn_DK_Player_GetCount) {
        return host.pfn_DK_Player_GetCount();
    }
    return 0;
}

inline bool CreateSaveBackup(const char* tag) {
    auto& host = PluginHost::Instance();
    if (host.Init() && host.pfn_DK_Backup_Create) {
        return host.pfn_DK_Backup_Create(tag);
    }
    return false;
}

inline bool ReloadConfig() {
    auto& host = PluginHost::Instance();
    if (host.Init() && host.pfn_DK_Config_Reload) {
        return host.pfn_DK_Config_Reload();
    }
    return false;
}

inline void BroadcastMessage(const char* message) {
    auto& host = PluginHost::Instance();
    if (host.Init() && host.pfn_DK_Player_Broadcast) {
        host.pfn_DK_Player_Broadcast(message);
    }
}

inline bool KickPlayer(uint32_t entityId, const char* reason = "Kicked by administrator") {
    auto& host = PluginHost::Instance();
    if (host.Init() && host.pfn_DK_Player_Kick) {
        return host.pfn_DK_Player_Kick(entityId, reason);
    }
    return false;
}

// ----------------------------------------------------------------------------
// DreamEventBus (Pub/Sub across mods)
// ----------------------------------------------------------------------------
namespace EventBus {
    inline uint64_t Subscribe(const char* eventName, void (*cb)(const char*, const void*, size_t, void*), void* userData = nullptr) {
        auto& host = PluginHost::Instance();
        if (host.Init() && host.pfn_DK_Event_Subscribe) {
            return host.pfn_DK_Event_Subscribe(eventName, cb, userData);
        }
        return 0;
    }

    inline bool Unsubscribe(uint64_t subId) {
        auto& host = PluginHost::Instance();
        if (host.Init() && host.pfn_DK_Event_Unsubscribe) {
            return host.pfn_DK_Event_Unsubscribe(subId);
        }
        return false;
    }

    inline size_t Publish(const char* eventName, const void* data = nullptr, size_t size = 0) {
        auto& host = PluginHost::Instance();
        if (host.Init() && host.pfn_DK_Event_Publish) {
            return host.pfn_DK_Event_Publish(eventName, data, size);
        }
        return 0;
    }

    inline size_t PublishString(const char* eventName, const std::string& message) {
        return Publish(eventName, message.data(), message.size());
    }
}

// ----------------------------------------------------------------------------
// DreamConfig::Bind (BepInEx style)
// ----------------------------------------------------------------------------
template <typename T>
class ModConfigEntry {
public:
    ModConfigEntry(const std::string& section, const std::string& key, const T& defaultVal, const std::string& desc)
        : m_section(section), m_key(key), m_fullKey(section.empty() ? key : (section + "." + key)),
          m_defaultVal(defaultVal), m_desc(desc) {
        Bind();
    }

    T Get() const;
    void Set(const T& val);

    operator T() const { return Get(); }
    T operator*() const { return Get(); }

    const std::string& GetSection() const { return m_section; }
    const std::string& GetKey() const { return m_key; }
    const std::string& GetFullKey() const { return m_fullKey; }
    const std::string& GetDescription() const { return m_desc; }
    const T& GetDefault() const { return m_defaultVal; }

private:
    void Bind();

    std::string m_section;
    std::string m_key;
    std::string m_fullKey;
    T m_defaultVal{};
    std::string m_desc;
};

template <>
inline void ModConfigEntry<float>::Bind() {
    auto& host = PluginHost::Instance();
    if (host.Init() && host.pfn_DK_Config_BindFloat) {
        host.pfn_DK_Config_BindFloat(m_section.c_str(), m_key.c_str(), m_defaultVal, m_desc.c_str());
    }
}
template <>
inline float ModConfigEntry<float>::Get() const {
    auto& host = PluginHost::Instance();
    if (host.Init() && host.pfn_DK_Config_GetFloat) {
        return host.pfn_DK_Config_GetFloat(m_fullKey.c_str(), m_defaultVal);
    }
    return m_defaultVal;
}
template <>
inline void ModConfigEntry<float>::Set(const float& val) {
    auto& host = PluginHost::Instance();
    if (host.Init() && host.pfn_DK_Config_SetFloat) {
        host.pfn_DK_Config_SetFloat(m_fullKey.c_str(), val);
    }
}

template <>
inline void ModConfigEntry<int>::Bind() {
    auto& host = PluginHost::Instance();
    if (host.Init() && host.pfn_DK_Config_BindInt) {
        host.pfn_DK_Config_BindInt(m_section.c_str(), m_key.c_str(), m_defaultVal, m_desc.c_str());
    }
}
template <>
inline int ModConfigEntry<int>::Get() const {
    auto& host = PluginHost::Instance();
    if (host.Init() && host.pfn_DK_Config_GetInt) {
        return host.pfn_DK_Config_GetInt(m_fullKey.c_str(), m_defaultVal);
    }
    return m_defaultVal;
}
template <>
inline void ModConfigEntry<int>::Set(const int& val) {
    auto& host = PluginHost::Instance();
    if (host.Init() && host.pfn_DK_Config_SetInt) {
        host.pfn_DK_Config_SetInt(m_fullKey.c_str(), val);
    }
}

template <>
inline void ModConfigEntry<bool>::Bind() {
    auto& host = PluginHost::Instance();
    if (host.Init() && host.pfn_DK_Config_BindBool) {
        host.pfn_DK_Config_BindBool(m_section.c_str(), m_key.c_str(), m_defaultVal, m_desc.c_str());
    }
}
template <>
inline bool ModConfigEntry<bool>::Get() const {
    auto& host = PluginHost::Instance();
    if (host.Init() && host.pfn_DK_Config_GetBool) {
        return host.pfn_DK_Config_GetBool(m_fullKey.c_str(), m_defaultVal);
    }
    return m_defaultVal;
}
template <>
inline void ModConfigEntry<bool>::Set(const bool& val) {
    auto& host = PluginHost::Instance();
    if (host.Init() && host.pfn_DK_Config_SetBool) {
        host.pfn_DK_Config_SetBool(m_fullKey.c_str(), val);
    }
}

namespace DreamConfig {
    template <typename T>
    inline ModConfigEntry<T> Bind(const std::string& section, const std::string& key, const T& defaultValue, const std::string& description = "") {
        return ModConfigEntry<T>(section, key, defaultValue, description);
    }
}

} // namespace dreamkurumi

