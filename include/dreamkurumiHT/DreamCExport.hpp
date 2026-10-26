#pragma once
#include <windows.h>
#include <cstdint>
#include <cstddef>
#include "dreamkurumiHT.hpp"

#ifdef DREAMKURUMI_EXPORTS
#define DK_API __declspec(dllexport)
#else
#define DK_API __declspec(dllimport)
#endif

extern "C" {

// ============================================================================
// Core Lifecycle
// ============================================================================
DK_API bool DK_Initialize(const char* cachePath) {
    return dreamkurumi::DreamCache::Instance().Initialize(cachePath ? cachePath : "dreamkurumi_cache.ini");
}

DK_API size_t DK_PatchAll() {
    return dreamkurumi::DreamHarmony::PatchAll();
}

DK_API void DK_UnpatchAll() {
    dreamkurumi::DreamHarmony::UnpatchAll();
}

// ============================================================================
// Symbol & Pattern Resolution
// ============================================================================
DK_API void* DK_Resolve(const char* symbolName, uintptr_t fallbackRva) {
    if (!symbolName) return nullptr;
    return dreamkurumi::DreamCache::Instance().Resolve(symbolName, fallbackRva);
}

DK_API void* DK_ResolvePattern(const char* symbolName, const char* pattern, uintptr_t fallbackRva) {
    if (!symbolName || !pattern) return nullptr;
    return dreamkurumi::DreamCache::Instance().ResolvePattern(symbolName, pattern, fallbackRva);
}

DK_API uintptr_t DK_GetModuleBase() {
    return dreamkurumi::DreamCache::Instance().GetModuleBase();
}

DK_API uint32_t DK_GetGameBuildTimestamp() {
    return dreamkurumi::DreamCache::Instance().GetGameBuildTimestamp();
}

// ============================================================================
// Raw Inline Hooking
// ============================================================================
DK_API void* DK_InlineHook(void* target, void* detour, void** ppOriginal) {
    if (!target || !detour) return nullptr;
    auto* hook = new dreamkurumi::DreamInlineHook();
    if (hook->Install(target, detour)) {
        if (ppOriginal) {
            *ppOriginal = hook->GetOriginal();
        }
        return hook;
    }
    delete hook;
    return nullptr;
}

DK_API bool DK_Unhook(void* hookHandle) {
    if (!hookHandle) return false;
    auto* hook = static_cast<dreamkurumi::DreamInlineHook*>(hookHandle);
    bool ok = hook->Disable();
    delete hook;
    return ok;
}

// ============================================================================
// Mid-Function Hooking (Register Interception)
// ============================================================================
using DK_MidHookCallback = void (*)(dreamkurumi::DreamCpuContext* ctx);

DK_API void* DK_MidHook(void* target, DK_MidHookCallback cb) {
    if (!target || !cb) return nullptr;
    auto* hook = new dreamkurumi::DreamMidHook();
    // Wrap to match MidHookCallback signature
    auto lambdaCb = reinterpret_cast<dreamkurumi::MidHookCallback>(cb);
    if (hook->Install(target, lambdaCb)) {
        return hook;
    }
    delete hook;
    return nullptr;
}

DK_API bool DK_MidUnhook(void* hookHandle) {
    if (!hookHandle) return false;
    auto* hook = static_cast<dreamkurumi::DreamMidHook*>(hookHandle);
    bool ok = hook->Disable();
    delete hook;
    return ok;
}

// Multi-Subscriber Chained Hook Pipeline
DK_API bool DK_RegisterChainedMidHook(void* target, DK_MidHookCallback cb, int priority, const char* name) {
    if (!target || !cb) return false;
    auto lambdaCb = [cb](dreamkurumi::DreamCpuContext& ctx) {
        cb(&ctx);
    };
    return dreamkurumi::DreamChainedHookPipeline::Instance().Register(target, lambdaCb, priority, name ? name : "");
}

// Anti-Crash Sandbox Shield
DK_API bool DK_ExecuteProtected(const char* modName, void (*cb)(void* userData), void* userData) {
    if (!cb) return false;
    return dreamkurumi::DreamCrashGuard::ExecuteSafely(modName ? modName : "PluginCallback", [&]() {
        cb(userData);
    });
}

// Dynamic Enshrouded ECS Resolver
DK_API void* DK_GetEntity(void* pContext, uint32_t entityId) {
    return dreamkurumi::enshrouded::EcsResolver::Instance().GetEntity(pContext, entityId);
}

DK_API void* DK_GetEntityComponent(void* pEntity, uint16_t compId) {
    return dreamkurumi::enshrouded::EcsResolver::Instance().GetComponent(pEntity, compId);
}

DK_API bool DK_GetEntityPosition(void* pEntity, float* outX, float* outY, float* outZ) {
    return dreamkurumi::enshrouded::EcsResolver::Instance().GetEntityPosition(pEntity, outX, outY, outZ);
}

// ============================================================================
// Memory Patching (NOP & Direct Bytes)
// ============================================================================
DK_API void* DK_PatchBytes(void* target, const uint8_t* bytes, size_t count) {
    if (!target || !bytes || count == 0) return nullptr;
    auto* patch = new dreamkurumi::DreamMemoryPatch();
    std::vector<uint8_t> vec(bytes, bytes + count);
    if (patch->PatchBytes(target, vec)) {
        return patch;
    }
    delete patch;
    return nullptr;
}

DK_API void* DK_Nop(void* target, size_t count) {
    if (!target || count == 0) return nullptr;
    auto* patch = new dreamkurumi::DreamMemoryPatch();
    if (patch->Nop(target, count)) {
        return patch;
    }
    delete patch;
    return nullptr;
}

DK_API bool DK_RestorePatch(void* patchHandle) {
    if (!patchHandle) return false;
    auto* patch = static_cast<dreamkurumi::DreamMemoryPatch*>(patchHandle);
    bool ok = patch->Restore();
    delete patch;
    return ok;
}

// ============================================================================
// Logging
// ============================================================================
DK_API void DK_SetLogCallback(void (*cb)(const char* msg)) {
    dreamkurumi::DreamLog::Instance().SetExternalCallback(cb);
}

DK_API void DK_Log(int level, const char* msg) {
    if (!msg) return;
    dreamkurumi::DreamLog::Instance().Log(static_cast<dreamkurumi::LogLevel>(level), "%s", msg);
}

// ============================================================================
// Presets & In-Game Overlay
// ============================================================================
DK_API void DK_Preset_SetGodMode(bool enable) {
    dreamkurumi::DreamPresets::Instance().SetGodMode(enable);
}

DK_API void DK_Preset_SetGhostMode(bool enable) {
    dreamkurumi::DreamPresets::Instance().SetGhostMode(enable);
}

DK_API void DK_Preset_SetInfiniteStamina(bool enable) {
    dreamkurumi::DreamPresets::Instance().SetInfiniteStamina(enable);
}

DK_API void DK_Preset_SetInfiniteMana(bool enable) {
    dreamkurumi::DreamPresets::Instance().SetInfiniteMana(enable);
}

DK_API void DK_Preset_SetInfiniteShroud(bool enable) {
    dreamkurumi::DreamPresets::Instance().SetInfiniteShroud(enable);
}

DK_API void DK_Preset_SetFreeCraft(bool enable) {
    dreamkurumi::DreamPresets::Instance().SetFreeCraft(enable);
}

DK_API void DK_Preset_SetSuperMining(bool enable, float mult) {
    dreamkurumi::DreamPresets::Instance().SetSuperMining(enable, mult);
}

DK_API void DK_Preset_SetUnlimitedFlight(bool enable) {
    dreamkurumi::DreamPresets::Instance().SetUnlimitedFlight(enable);
}

DK_API void DK_Preset_SetInfiniteItemSplit(bool enable) {
    dreamkurumi::DreamPresets::Instance().SetInfiniteItemSplit(enable);
}

DK_API void DK_Preset_SetInfiniteItemUse(bool enable) {
    dreamkurumi::DreamPresets::Instance().SetInfiniteItemUse(enable);
}

DK_API void DK_Preset_SetNoResourceCost(bool enable) {
    dreamkurumi::DreamPresets::Instance().SetNoResourceCost(enable);
}

DK_API void DK_Preset_SetInfiniteDurability(bool enable) {
    dreamkurumi::DreamPresets::Instance().SetInfiniteDurability(enable);
}

DK_API void DK_Preset_SetOneShotKill(bool enable) {
    dreamkurumi::DreamPresets::Instance().SetOneShotKill(enable);
}

DK_API void DK_Preset_SetFastProduction(bool enable, float seconds) {
    dreamkurumi::DreamPresets::Instance().SetFastProduction(enable, seconds);
}

DK_API bool DK_Preset_SaveConfig(const char* path) {
    return dreamkurumi::DreamPresets::Instance().SaveConfig(path ? path : "dreamkurumi_config.ini");
}

DK_API bool DK_Preset_LoadConfig(const char* path) {
    return dreamkurumi::DreamPresets::Instance().LoadConfig(path ? path : "dreamkurumi_config.ini");
}

DK_API bool DK_Preset_IsDedicatedServer() {
    return dreamkurumi::DreamServerGuard::IsDedicatedServer();
}

DK_API void DK_Overlay_ToggleMenu() {
    dreamkurumi::DreamOverlay::Instance().ToggleMenu();
}

DK_API bool DK_Overlay_IsMenuOpen() {
    return dreamkurumi::DreamOverlay::Instance().IsMenuOpen();
}

DK_API void DK_Overlay_SetMenuKey(int vKey) {
    dreamkurumi::DreamOverlay::Instance().SetMenuToggleKey(vKey);
}

DK_API bool DK_Overlay_ExecuteCommand(const char* cmd) {
    if (!cmd) return false;
    return dreamkurumi::DreamOverlay::Instance().ExecuteCommand(cmd);
}

typedef void (*DK_CommandCallback)(const char* args);

DK_API void DK_Overlay_RegisterCommand(const char* cmdName, DK_CommandCallback callback, const char* desc) {
    if (!cmdName || !callback) return;
    dreamkurumi::DreamOverlay::Instance().RegisterCommand(cmdName, [callback](const std::string& args) {
        callback(args.c_str());
    }, desc ? desc : "");
}

// ============================================================================
// Server Policy & Anti-Cheat
// ============================================================================
DK_API void DK_Policy_EnforceStrictRules() {
    dreamkurumi::DreamServerPolicy::Instance().EnforceStrictServerRules();
}

DK_API void DK_Policy_AllowAllFeatures() {
    dreamkurumi::DreamServerPolicy::Instance().AllowAllFeatures();
}

DK_API void DK_Policy_SetFeatureAllowed(int feature, bool allowed) {
    if (feature >= 0 && feature < static_cast<int>(dreamkurumi::PresetFeature::Count)) {
        dreamkurumi::DreamServerPolicy::Instance().SetFeatureAllowed(
            static_cast<dreamkurumi::PresetFeature>(feature), allowed
        );
    }
}

DK_API bool DK_Policy_IsFeatureAllowed(int feature) {
    if (feature >= 0 && feature < static_cast<int>(dreamkurumi::PresetFeature::Count)) {
        return dreamkurumi::DreamServerPolicy::Instance().IsFeatureAllowed(
            static_cast<dreamkurumi::PresetFeature>(feature)
        );
    }
    return false;
}

DK_API bool DK_Policy_SavePolicy(const char* path) {
    return dreamkurumi::DreamServerPolicy::Instance().SavePolicy(path ? path : "server_policy.ini");
}

DK_API bool DK_Policy_LoadPolicy(const char* path) {
    return dreamkurumi::DreamServerPolicy::Instance().LoadPolicy(path ? path : "server_policy.ini");
}

// ============================================================================
// Micro-Profiler
// ============================================================================
DK_API void DK_Profiler_PrintReport() {
    dreamkurumi::DreamProfiler::Instance().PrintReport();
}

DK_API void DK_Profiler_Reset() {
    dreamkurumi::DreamProfiler::Instance().Reset();
}

DK_API void DK_Profiler_SetEnabled(bool enable) {
    dreamkurumi::DreamProfiler::Instance().SetEnabled(enable);
}

// ============================================================================
// Environment & Dual-Side Detection
// ============================================================================
DK_API bool DK_Env_IsDedicatedServer() {
    return dreamkurumi::DreamEnv::IsDedicatedServer();
}

DK_API bool DK_Env_IsClient() {
    return dreamkurumi::DreamEnv::IsClient();
}

// ============================================================================
// Hash Engine
// ============================================================================
DK_API uint64_t DK_Hash_Fnv1a64(const char* str) {
    return dreamkurumi::DreamHash::Hash(str);
}

// ============================================================================
// Watchdog (Anti-Hang)
// ============================================================================
DK_API void DK_Watchdog_Heartbeat() {
    dreamkurumi::DreamWatchdog::Instance().Heartbeat();
}

DK_API void DK_Watchdog_RegisterThread(const char* name) {
    dreamkurumi::DreamWatchdog::Instance().RegisterThread(GetCurrentThreadId(), name ? name : "");
}

// ============================================================================
// NetBridge (Client <-> Server Mod Sync)
// ============================================================================
DK_API bool DK_Net_SendToServer(uint64_t channelHash, uint16_t opcode, const uint8_t* payload, uint32_t payloadSize) {
    dreamkurumi::NetBufferWriter writer;
    if (payload && payloadSize > 0) {
        writer.WriteBytes(payload, payloadSize);
    }
    return dreamkurumi::DreamNetBridge::Instance().SendToServer(channelHash, opcode, writer);
}

DK_API bool DK_Net_BroadcastToClients(uint64_t channelHash, uint16_t opcode, const uint8_t* payload, uint32_t payloadSize) {
    dreamkurumi::NetBufferWriter writer;
    if (payload && payloadSize > 0) {
        writer.WriteBytes(payload, payloadSize);
    }
    return dreamkurumi::DreamNetBridge::Instance().BroadcastToClients(channelHash, opcode, writer);
}

DK_API void DK_Net_SyncSetVar(const char* key, const char* value) {
    if (key && value) {
        dreamkurumi::DreamServerSync::Instance().SetSyncVar(key, value);
    }
}

DK_API bool DK_Net_SyncGetVar(const char* key, char* outBuffer, uint32_t outSize) {
    if (!key || !outBuffer || outSize == 0) return false;
    std::string val = dreamkurumi::DreamServerSync::Instance().GetSyncVar(key, "");
    if (val.empty()) return false;
    strncpy_s(outBuffer, outSize, val.c_str(), _TRUNCATE);
    return true;
}

// ============================================================================
// GameContext (Service Locator & Player Actions)
// ============================================================================
DK_API void* DK_Game_GetLocalPlayer() {
    return dreamkurumi::DreamGameContext::Instance().GetLocalPlayer();
}

DK_API void DK_Game_SetLocalPlayer(void* playerPtr) {
    dreamkurumi::DreamGameContext::Instance().SetLocalPlayer(playerPtr);
}

DK_API bool DK_Game_IsPlayerReady() {
    return dreamkurumi::DreamGameContext::Instance().IsPlayerReady();
}

DK_API bool DK_Game_TeleportPlayer(float x, float y, float z) {
    return dreamkurumi::DreamGameContext::Instance().TeleportPlayer(x, y, z);
}

DK_API void DK_Game_SetGodMode(bool enabled) {
    dreamkurumi::DreamGameContext::Instance().SetGodMode(enabled);
}

DK_API void DK_Game_SetGhostMode(bool enabled) {
    dreamkurumi::DreamGameContext::Instance().SetGhostMode(enabled);
}

DK_API void DK_Game_SetInfiniteStamina(bool enabled) {
    dreamkurumi::DreamGameContext::Instance().SetInfiniteStamina(enabled);
}

// ============================================================================
// Player Registry & Session Management
// ============================================================================
DK_API void DK_Player_OnLogin(uint32_t entityId, const char* name, bool isAdmin) {
    dreamkurumi::DreamPlayerRegistry::Instance().OnPlayerLogin(entityId, name ? name : "", isAdmin);
}

DK_API void DK_Player_OnDisconnect(uint32_t entityId) {
    dreamkurumi::DreamPlayerRegistry::Instance().OnPlayerDisconnect(entityId);
}

DK_API uint32_t DK_Player_GetCount() {
    return static_cast<uint32_t>(dreamkurumi::DreamPlayerRegistry::Instance().GetPlayerCount());
}

DK_API void DK_Player_Broadcast(const char* message) {
    if (message) {
        dreamkurumi::DreamPlayerRegistry::Instance().Broadcast(message);
    }
}

DK_API bool DK_Player_Kick(uint32_t entityId, const char* reason) {
    return dreamkurumi::DreamPlayerRegistry::Instance().Kick(entityId, reason ? reason : "Kicked by administrator");
}

using DK_PlayerKickCallback = void (*)(uint32_t entityId, const char* reason);
using DK_PlayerBroadcastCallback = void (*)(const char* message);

DK_API void DK_Player_SetKickCallback(DK_PlayerKickCallback cb) {
    if (cb) {
        dreamkurumi::DreamPlayerRegistry::Instance().SetKickCallback([cb](uint32_t id, const std::string& reason) {
            cb(id, reason.c_str());
        });
    }
}

DK_API void DK_Player_SetBroadcastCallback(DK_PlayerBroadcastCallback cb) {
    if (cb) {
        dreamkurumi::DreamPlayerRegistry::Instance().SetBroadcastCallback([cb](const std::string& msg) {
            cb(msg.c_str());
        });
    }
}

// ============================================================================
// Savegame Hot Backup
// ============================================================================
DK_API bool DK_Backup_Create(const char* tag) {
    auto res = dreamkurumi::DreamBackup::Instance().CreateBackup(tag ? tag : "");
    return res.success;
}

// ============================================================================
// Live Configuration Hot-Reload
// ============================================================================
DK_API bool DK_Config_Reload() {
    return dreamkurumi::DreamLiveConfig::Instance().ReloadFromFiles();
}

// ============================================================================
// Event Bus Pub/Sub
// ============================================================================
using DK_EventCallback = void (*)(const char* eventName, const void* data, size_t size, void* userData);

DK_API uint64_t DK_Event_Subscribe(const char* eventName, DK_EventCallback cb, void* userData) {
    if (!eventName || !cb) return 0;
    return dreamkurumi::DreamEventBus::Instance().Subscribe(eventName, [cb, userData](const dreamkurumi::DreamEvent& ev) {
        cb(ev.name.c_str(), ev.data, ev.size, userData);
    });
}

DK_API bool DK_Event_Unsubscribe(uint64_t subId) {
    return dreamkurumi::DreamEventBus::Instance().Unsubscribe(subId);
}

DK_API size_t DK_Event_Publish(const char* eventName, const void* data, size_t size) {
    if (!eventName) return 0;
    return dreamkurumi::DreamEventBus::Instance().Publish(eventName, data, size);
}

// ============================================================================
// Live Config Binding (BepInEx style)
// ============================================================================
DK_API void DK_Config_BindFloat(const char* section, const char* key, float defaultVal, const char* desc) {
    if (section && key) {
        dreamkurumi::DreamLiveConfig::Instance().Bind(section, key, defaultVal, desc ? desc : "");
    }
}

DK_API void DK_Config_BindInt(const char* section, const char* key, int defaultVal, const char* desc) {
    if (section && key) {
        dreamkurumi::DreamLiveConfig::Instance().Bind(section, key, defaultVal, desc ? desc : "");
    }
}

DK_API void DK_Config_BindBool(const char* section, const char* key, bool defaultVal, const char* desc) {
    if (section && key) {
        dreamkurumi::DreamLiveConfig::Instance().Bind(section, key, defaultVal, desc ? desc : "");
    }
}

DK_API void DK_Config_BindString(const char* section, const char* key, const char* defaultVal, const char* desc) {
    if (section && key) {
        dreamkurumi::DreamLiveConfig::Instance().Bind(section, key, defaultVal ? std::string(defaultVal) : "", desc ? desc : "");
    }
}

DK_API float DK_Config_GetFloat(const char* fullKey, float fallback) {
    return fullKey ? dreamkurumi::DreamLiveConfig::Instance().GetFloat(fullKey, fallback) : fallback;
}

DK_API void DK_Config_SetFloat(const char* fullKey, float val) {
    if (fullKey) {
        std::ostringstream ss;
        ss << std::fixed << std::setprecision(2) << val;
        dreamkurumi::DreamLiveConfig::Instance().Set(fullKey, ss.str());
    }
}

DK_API int DK_Config_GetInt(const char* fullKey, int fallback) {
    return fullKey ? dreamkurumi::DreamLiveConfig::Instance().GetInt(fullKey, fallback) : fallback;
}

DK_API void DK_Config_SetInt(const char* fullKey, int val) {
    if (fullKey) {
        dreamkurumi::DreamLiveConfig::Instance().Set(fullKey, std::to_string(val));
    }
}

DK_API bool DK_Config_GetBool(const char* fullKey, bool fallback) {
    return fullKey ? dreamkurumi::DreamLiveConfig::Instance().GetBool(fullKey, fallback) : fallback;
}

DK_API void DK_Config_SetBool(const char* fullKey, bool val) {
    if (fullKey) {
        dreamkurumi::DreamLiveConfig::Instance().Set(fullKey, val ? "true" : "false");
    }
}

// ============================================================================
// Harmony Native Engine
// ============================================================================
DK_API size_t DK_Harmony_PatchAll() {
    return dreamkurumi::DreamHarmony::PatchAll();
}

DK_API void DK_Harmony_UnpatchAll() {
    dreamkurumi::DreamHarmony::UnpatchAll();
}

} // extern "C"

