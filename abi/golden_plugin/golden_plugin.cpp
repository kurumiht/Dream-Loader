// golden_plugin.cpp - "golden plugin" for the dreamkurumiHT plugin ABI.
//
// Built ONCE per ABI release against the FROZEN copy of the plugin header (abi/golden/<tag>/dreamkurumiHT_plugin.h,
// created by abi/freeze_header.sh) and then loaded by the CURRENT core in CI (abi/golden_host_test.cpp).
// If a new core still resolves every symbol this old plugin binds, old plugins keep working.
//
// This file is GENERATED from the BIND_DK list of the frozen header; regenerate with abi/make_golden_plugin.py.
// Build (MinGW):  g++ -std=c++20 -shared -O1 -I abi/golden/<tag> abi/golden_plugin/golden_plugin.cpp -o golden_plugin_<tag>.dll
// Do NOT commit the resulting DLL (build it in CI, cache as a CI artifact).

#include "dreamkurumiHT_plugin.h"
#include <cstdio>
#include <cstring>

#ifdef _WIN32
#define GOLDEN_EXPORT extern "C" __declspec(dllexport)
#else
#define GOLDEN_EXPORT extern "C"
#endif

using namespace dreamkurumi;

// Returns 0 when the core resolved everything this plugin version binds.
// Otherwise returns the number of unresolved symbols and writes their names (comma separated) into err.
GOLDEN_EXPORT int GoldenSelfTest(char* err, int cap) {
    auto& h = PluginHost::Instance();
    if (!h.Init()) { if (err && cap) std::snprintf(err, (size_t)cap, "PluginHost::Init failed (dreamkurumiHT.dll not found)"); return -1; }
    int missing = 0; std::string names;
#define CHECK(ptr, nm) do { if (!h.ptr) { ++missing; if (!names.empty()) names += ","; names += nm; } } while (0)
    CHECK(pfn_DK_Asset_ClearAll, "DK_Asset_ClearAll");
    CHECK(pfn_DK_Asset_GetOverride, "DK_Asset_GetOverride");
    CHECK(pfn_DK_Asset_IsValidKfc4, "DK_Asset_IsValidKfc4");
    CHECK(pfn_DK_Asset_RegisterLocaTag, "DK_Asset_RegisterLocaTag");
    CHECK(pfn_DK_Asset_RegisterOverride, "DK_Asset_RegisterOverride");
    CHECK(pfn_DK_Backup_Create, "DK_Backup_Create");
    CHECK(pfn_DK_ChainLoader_GetCount, "DK_ChainLoader_GetCount");
    CHECK(pfn_DK_ChainLoader_LoadChainedDll, "DK_ChainLoader_LoadChainedDll");
    CHECK(pfn_DK_ChainLoader_ScanAndLoad, "DK_ChainLoader_ScanAndLoad");
    CHECK(pfn_DK_Config_BindBool, "DK_Config_BindBool");
    CHECK(pfn_DK_Config_BindFloat, "DK_Config_BindFloat");
    CHECK(pfn_DK_Config_BindInt, "DK_Config_BindInt");
    CHECK(pfn_DK_Config_BindString, "DK_Config_BindString");
    CHECK(pfn_DK_Config_GetBool, "DK_Config_GetBool");
    CHECK(pfn_DK_Config_GetFloat, "DK_Config_GetFloat");
    CHECK(pfn_DK_Config_GetInt, "DK_Config_GetInt");
    CHECK(pfn_DK_Config_GetString, "DK_Config_GetString");
    CHECK(pfn_DK_Config_Reload, "DK_Config_Reload");
    CHECK(pfn_DK_Config_SetBool, "DK_Config_SetBool");
    CHECK(pfn_DK_Config_SetFloat, "DK_Config_SetFloat");
    CHECK(pfn_DK_Config_SetInt, "DK_Config_SetInt");
    CHECK(pfn_DK_Config_SetString, "DK_Config_SetString");
    CHECK(pfn_DK_CpuSupportsAvx, "DK_CpuSupportsAvx");
    CHECK(pfn_DK_Env_IsClient, "DK_Env_IsClient");
    CHECK(pfn_DK_Env_IsDedicatedServer, "DK_Env_IsDedicatedServer");
    CHECK(pfn_DK_Env_IsWine, "DK_Env_IsWine");
    CHECK(pfn_DK_Event_Publish, "DK_Event_Publish");
    CHECK(pfn_DK_Event_Subscribe, "DK_Event_Subscribe");
    CHECK(pfn_DK_Event_Unsubscribe, "DK_Event_Unsubscribe");
    CHECK(pfn_DK_ExecuteProtected, "DK_ExecuteProtected");
    CHECK(pfn_DK_GetEntity, "DK_GetEntity");
    CHECK(pfn_DK_GetEntityComponent, "DK_GetEntityComponent");
    CHECK(pfn_DK_GetEntityPosition, "DK_GetEntityPosition");
    CHECK(pfn_DK_GetGameBuildTimestamp, "DK_GetGameBuildTimestamp");
    CHECK(pfn_DK_GetModuleBase, "DK_GetModuleBase");
    CHECK(pfn_DK_GetWorldCursor, "DK_GetWorldCursor");
    CHECK(pfn_DK_GetWorldCursorPosition, "DK_GetWorldCursorPosition");
    CHECK(pfn_DK_GetWorldCursorRay, "DK_GetWorldCursorRay");
    CHECK(pfn_DK_Hook_HasExistingHook, "DK_Hook_HasExistingHook");
    CHECK(pfn_DK_Initialize, "DK_Initialize");
    CHECK(pfn_DK_InlineHook, "DK_InlineHook");
    CHECK(pfn_DK_Log, "DK_Log");
    CHECK(pfn_DK_MidHook, "DK_MidHook");
    CHECK(pfn_DK_MidUnhook, "DK_MidUnhook");
    CHECK(pfn_DK_Nop, "DK_Nop");
    CHECK(pfn_DK_Overlay_ExecuteCommand, "DK_Overlay_ExecuteCommand");
    CHECK(pfn_DK_Overlay_IsMenuOpen, "DK_Overlay_IsMenuOpen");
    CHECK(pfn_DK_Overlay_SetMenuKey, "DK_Overlay_SetMenuKey");
    CHECK(pfn_DK_Overlay_ToggleMenu, "DK_Overlay_ToggleMenu");
    CHECK(pfn_DK_PatchAll, "DK_PatchAll");
    CHECK(pfn_DK_PatchBytes, "DK_PatchBytes");
    CHECK(pfn_DK_Player_Broadcast, "DK_Player_Broadcast");
    CHECK(pfn_DK_Player_GetCount, "DK_Player_GetCount");
    CHECK(pfn_DK_Player_Kick, "DK_Player_Kick");
    CHECK(pfn_DK_Player_OnDisconnect, "DK_Player_OnDisconnect");
    CHECK(pfn_DK_Player_OnLogin, "DK_Player_OnLogin");
    CHECK(pfn_DK_Preset_IsDedicatedServer, "DK_Preset_IsDedicatedServer");
    CHECK(pfn_DK_Preset_LoadConfig, "DK_Preset_LoadConfig");
    CHECK(pfn_DK_Preset_SaveConfig, "DK_Preset_SaveConfig");
    CHECK(pfn_DK_Preset_SetFastProduction, "DK_Preset_SetFastProduction");
    CHECK(pfn_DK_Preset_SetFreeCraft, "DK_Preset_SetFreeCraft");
    CHECK(pfn_DK_Preset_SetGhostMode, "DK_Preset_SetGhostMode");
    CHECK(pfn_DK_Preset_SetGodMode, "DK_Preset_SetGodMode");
    CHECK(pfn_DK_Preset_SetInfiniteDurability, "DK_Preset_SetInfiniteDurability");
    CHECK(pfn_DK_Preset_SetInfiniteItemSplit, "DK_Preset_SetInfiniteItemSplit");
    CHECK(pfn_DK_Preset_SetInfiniteItemUse, "DK_Preset_SetInfiniteItemUse");
    CHECK(pfn_DK_Preset_SetInfiniteMana, "DK_Preset_SetInfiniteMana");
    CHECK(pfn_DK_Preset_SetInfiniteShroud, "DK_Preset_SetInfiniteShroud");
    CHECK(pfn_DK_Preset_SetInfiniteStamina, "DK_Preset_SetInfiniteStamina");
    CHECK(pfn_DK_Preset_SetNoResourceCost, "DK_Preset_SetNoResourceCost");
    CHECK(pfn_DK_Preset_SetOneShotKill, "DK_Preset_SetOneShotKill");
    CHECK(pfn_DK_Preset_SetSuperMining, "DK_Preset_SetSuperMining");
    CHECK(pfn_DK_Preset_SetUnlimitedFlight, "DK_Preset_SetUnlimitedFlight");
    CHECK(pfn_DK_Profiler_PrintReport, "DK_Profiler_PrintReport");
    CHECK(pfn_DK_Profiler_Reset, "DK_Profiler_Reset");
    CHECK(pfn_DK_Profiler_SetEnabled, "DK_Profiler_SetEnabled");
    CHECK(pfn_DK_RegisterChainedMidHook, "DK_RegisterChainedMidHook");
    CHECK(pfn_DK_Resolve, "DK_Resolve");
    CHECK(pfn_DK_ResolvePattern, "DK_ResolvePattern");
    CHECK(pfn_DK_RestorePatch, "DK_RestorePatch");
    CHECK(pfn_DK_Scheduler_Cancel, "DK_Scheduler_Cancel");
    CHECK(pfn_DK_Scheduler_Enqueue, "DK_Scheduler_Enqueue");
    CHECK(pfn_DK_Scheduler_RunAfter, "DK_Scheduler_RunAfter");
    CHECK(pfn_DK_Scheduler_Tick, "DK_Scheduler_Tick");
    CHECK(pfn_DK_SetWorldCursor, "DK_SetWorldCursor");
    CHECK(pfn_DK_ShowToast, "DK_ShowToast");
    CHECK(pfn_DK_Unhook, "DK_Unhook");
    CHECK(pfn_DK_UnpatchAll, "DK_UnpatchAll");
    CHECK(pfn_DK_Vulkan_ConfigureLayerEnvironment, "DK_Vulkan_ConfigureLayerEnvironment");
    CHECK(pfn_DK_Vulkan_GenerateLayerManifest, "DK_Vulkan_GenerateLayerManifest");
    CHECK(pfn_DK_Watchdog_Heartbeat, "DK_Watchdog_Heartbeat");
    CHECK(pfn_DK_Watchdog_RegisterThread, "DK_Watchdog_RegisterThread");
#undef CHECK
    if (err && cap) std::snprintf(err, (size_t)cap, "%s", names.c_str());
    return missing;
}

// A call through the real C ABI: logging must not crash and must accept the old signature.
GOLDEN_EXPORT int GoldenSmokeCalls() {
    auto& h = PluginHost::Instance();
    if (!h.Init()) return -1;
    if (h.pfn_DK_Log) h.pfn_DK_Log(1, "golden plugin: ABI smoke call");
    if (h.pfn_DK_GetModuleBase && h.pfn_DK_GetModuleBase() == 0) return 1;   // 0 is allowed in a test host: informational
    return 0;
}
