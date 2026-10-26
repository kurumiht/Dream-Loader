# dreamkurumiHT - Mod Developer Comprehensive Guide
> **The Definitive Modding Guide for Enshrouded (C++20 & C# .NET Native AOT)**  
> Framework Version: `3.4.0` | Architecture: x64 Windows | Last Updated: October 2026  
> [🇹🇭 สลับไปยังคู่มือฉบับภาษาไทย (DEVELOPER_GUIDE.md)](DEVELOPER_GUIDE.md)

---

## Table of Contents
1. [Core Architectural Rules & Design Principles](#1-core-architectural-rules--design-principles)
   - 1.1 [Rule 1: Strict Architectural Separation of dreamkurumiHT (Engine) and Mods](#11-rule-1-strict-architectural-separation-of-dreamkurumiht-engine-and-mods)
   - 1.2 [Rule 2: Universal Server Access vs Admin-Whitelist Policy](#12-rule-2-universal-server-access-vs-admin-whitelist-policy)
   - 1.3 [Standard Directory Layout (Client & Dedicated Server)](#13-standard-directory-layout-client--dedicated-server)
2. [Dynamic C-ABI Plugin Binding Interface (`dreamkurumiHT_plugin.h`)](#2-dynamic-c-abi-plugin-binding-interface-dreamkurumiht_pluginh)
   - 2.1 [Why Use the Dynamic Plugin Interface (~30-180KB Footprint)](#21-why-use-the-dynamic-plugin-interface-30-180kb-footprint)
   - 2.2 [PluginHost Structure and Exported Symbols](#22-pluginhost-structure-and-exported-symbols)
3. [Mod Manifest Specification (`modinfo.json` & `ModInfo.xml`)](#3-mod-manifest-specification-modinfojson--modinfoxml)
4. [Quickstart: Create Your First Mod in 15 Lines](#4-quickstart-create-your-first-mod-in-15-lines)
   - 4.1 [Method A: Dynamic C-ABI Plugin (Recommended for Modular Mods)](#41-method-a-dynamic-c-abi-plugin-recommended-for-modular-mods)
   - 4.2 [Method B: Direct Header-Only Macro (`dreamkurumiHT.hpp`)](#42-method-b-direct-header-only-macro-dreamkurumihthpp)
5. [Complete Harmony Hooking Guide](#5-complete-harmony-hooking-guide)
   - 5.1 [AOB Signature Scanning & Multi-Tier Caching](#51-aob-signature-scanning--multi-tier-caching)
   - 5.2 [Inline Hooking with Near-Memory Trampoline Pool (64KB Arena)](#52-inline-hooking-with-near-memory-trampoline-pool-64kb-arena)
   - 5.3 [Chained Mid-Hooking with Priority Scheduling (Zero Mod Conflict)](#53-chained-mid-hooking-with-priority-scheduling-zero-mod-conflict)
   - 5.4 [Skipping Original Functions & Modifying Arguments/Returns](#54-skipping-original-functions--modifying-argumentsreturns)
   - 5.5 [Hardware Breakpoint VEH Hooking (Zero-Byte Code Modification)](#55-hardware-breakpoint-veh-hooking-zero-byte-code-modification)
   - 5.6 [Mid-Function Register Hooking (Access CPU Context: RAX, RCX, XMM)](#56-mid-function-register-hooking-access-cpu-context-rax-rcx-xmm)
   - 5.7 [VTable & RTTI Hooking (Intercept C++ Class Virtual Methods)](#57-vtable--rtti-hooking-intercept-c-class-virtual-methods)
6. [100% Safe Memory Management & Pointer Chains](#6-100-safe-memory-management--pointer-chains)
   - 6.1 [Sandbox Memory Safe Read/Write (Zero Access Violation Crashes)](#61-sandbox-memory-safe-readwrite-zero-access-violation-crashes)
   - 6.2 [Multi-Level Pointer Chains (`ReadChain` / `WriteChain`)](#62-multi-level-pointer-chains-readchain--writechain)
   - 6.3 [RAII Byte Patching & NOPs (`DreamBytePatch`)](#63-raii-byte-patching--nops-dreambytepatch)
7. [Asynchronous Double-Buffered Logging Engine (`DreamLog`)](#7-asynchronous-double-buffered-logging-engine-dreamlog)
   - 7.1 [Double-Buffer Queue Architecture & Background Worker (<1µs Overhead)](#71-double-buffer-queue-architecture--background-worker-1µs-overhead)
   - 7.2 [Anti-Spam Deduplication Filter (2.0s Sliding Window)](#72-anti-spam-deduplication-filter-20s-sliding-window)
   - 7.3 [Channel Verbosity & Timed Burst Debugging](#73-channel-verbosity--timed-burst-debugging)
   - 7.4 [In-Memory Ring Buffer & Live Grep / Tail Logs](#74-in-memory-ring-buffer--live-grep--tail-logs)
8. [Dynamic Runtime Configuration (`DreamLiveConfig`)](#8-dynamic-runtime-configuration-dreamliveconfig)
   - 8.1 [Registering Config Properties (`RegisterFloat`, `RegisterBool`, etc.)](#81-registering-config-properties-registerfloat-registerbool-etc)
   - 8.2 [Hot-Reload Callbacks: Real-time Memory Updates without Restart](#82-hot-reload-callbacks-real-time-memory-updates-without-restart)
9. [Interactive Diagnostics Console & CLI Overlay (`DreamOverlay`)](#9-interactive-diagnostics-console--cli-overlay-dreamoverlay)
   - 9.1 [Dual Mode: Windows GUI Console (`F10`) & Piped Stdin (Headless Daemon)](#91-dual-mode-windows-gui-console-f10--piped-stdin-headless-daemon)
   - 9.2 [Console Command Reference Table & Short Aliases](#92-console-command-reference-table--short-aliases)
   - 9.3 [Holistic Diagnostic Snapshots (`snapshot`)](#93-holistic-diagnostic-snapshots-snapshot)
10. [Game Engine & ECS Service Locator (`DreamGameContext`)](#10-game-engine--ecs-service-locator-dreamgamecontext)
    - 10.1 [Entity Component System (ECS) Inspection & 3D Coordinates](#101-entity-component-system-ecs-inspection--3d-coordinates)
    - 10.2 [Player Coordinate Tracking & Teleportation](#102-player-coordinate-tracking--teleportation)
    - 10.3 [Built-in Gameplay Presets (GodMode, InfiniteStamina, FreeCraft, etc.)](#103-built-in-gameplay-presets-godmode-infinitestamina-freecraft-etc)
11. [Cross-Platform Dedicated Server & Client Separation](#11-cross-platform-dedicated-server--client-separation)
    - 11.1 [Platform Macros (`DREAM_SERVER_ONLY` / `DREAM_CLIENT_ONLY`)](#111-platform-macros-dream_server_only--dream_client_only)
    - 11.2 [Network RPC Bridge (`DreamNetBridge`)](#112-network-rpc-bridge-dreamnetbridge)
    - 11.3 [Server-to-Client Config Broadcasting (`DreamServerSync`)](#113-server-to-client-config-broadcasting-dreamserversync)
12. [C# .NET 8/10 Developer Guide (Native AOT)](#12-c-net-810-developer-guide-native-aot)
13. [High-Speed JSON Engine (DreamJson Powered by yyjson)](#13-high-speed-json-engine-dreamjson-powered-by-yyjson)
14. [In-Memory Scripting Engine (DreamLua Powered by LuaJIT 2.1)](#14-in-memory-scripting-engine-dreamlua-powered-by-luajit-21)
15. [Automated Build & Sync Engine (`dev_sync.ps1`)](#15-automated-build--sync-engine-dev_syncps1)
    - 15.1 [One-Click Multi-Target Build & Deploy](#151-one-click-multi-target-build--deploy)
    - 15.2 [Hot-Replacement Trick for In-Use DLLs (`.locked_*`)](#152-hot-replacement-trick-for-in-use-dlls-locked_)
16. [Best Practices, Performance Guarantees & Troubleshooting](#16-best-practices-performance-guarantees--troubleshooting)

---

## 1. Core Architectural Rules & Design Principles

### 1.1 Rule 1: Strict Architectural Separation of dreamkurumiHT (Engine) and Mods
* **`dreamkurumiHT` (`dreamkurumiHT.dll`) is exclusively the Native Harmony Hooking Framework / Core Engine:**
  * Its single responsibility is providing hooking primitives (`InlineHook`, `MidHook`, `PatchBytes`), multi-tier AOB pattern scanning, near-memory 64KB trampoline pools, hang/deadlock watchdogs, async double-buffered logging, and dynamic plugin dispatching.
  * **Strict Policy:** **NEVER embed game-specific mod logic (such as crop harvesting, loot gathering, party EXP sharing, or fishing utilities) into `dreamkurumiHT.dll`!**
  * **Core Principle:** **Mod is Mod, Dream is Harmony Engine.** They must always remain decoupled.
* **Every mod must be modular and live in its own independent directory:**
  * `mods\dreamkurumiHT\` (`dreamkurumiHT.dll`): The single shared framework DLL.
  * `mods\Dream-AoE-Harvest\` (`DreamAoEHarvest.dll` + `config.ini`): Dedicated farming crop harvest mod (3.5m radius, F8 toggle).
  * `mods\Dream-Auto-Pickup\` (`DreamAutoPickup.dll` + `config.ini`): Dedicated ground loot, monster drops, logs, stone, and ore collection mod (5.0m radius, F7 toggle).
  * `mods\Admin-Control-Server\` (`AdminControlServer.dll` + `admin_fishing_config.ini`): Dedicated fishing and admin utilities.
  * `mods\XP-Share\` (`XPShare.dll` + `config.ini`): Dedicated party EXP distribution.
* Mods dynamically link to `dreamkurumiHT.dll` via the lightweight C-ABI plugin header (`dreamkurumiHT_plugin.h`).

---

### 1.2 Rule 2: Universal Server Access vs Admin-Whitelist Policy
* **Universal Player Access by Default:**
  * All gameplay and quality-of-life mods (`Dream-AoE-Harvest`, `Dream-Auto-Pickup`, `XP-Share`, etc.) **MUST be fully accessible to ALL players connected to the dedicated server by default**.
  * Any `AdminOnly` configuration setting **must default to `false` (or 0)**.
  * Normal players must be able to harvest crops in AoE, pick up ground drops/monster loot in AoE, and share EXP with party members without needing admin permissions.
* **Admin-Whitelist Exception:**
  * Only specific administrative commands, cheat tools, or debug capabilities (e.g. GodMode, GhostMode, FreeCraft, Infinite Shroud/Mana, or instant admin fishing in `AdminControlServer`) are restricted to players verified on the Admin Whitelist (`adminGroupName` / `adminPlayerName` / SteamID).

---

### 1.3 Standard Directory Layout (Client & Dedicated Server)

```text
Enshrouded/ (or EnshroudedServer/)
├── enshrouded.exe (or enshrouded_server.exe)
├── dbghelp.dll                  <-- Mod loader proxy
├── mod_loader.log               <-- Loader diagnostic log
├── mods/
│   ├── dreamkurumiHT/
│   │   ├── dreamkurumiHT.dll    <-- Core Native Engine
│   │   ├── lua51.dll            <-- LuaJIT 2.1 x64 Runtime
│   │   ├── dreamkurumi.ini      <-- Watchdog, thread, log configs
│   │   └── dreamkurumi_cache.ini<-- Cached AOB signatures (0.0001ms instant launch)
│   ├── Dream-AoE-Harvest/
│   │   ├── DreamAoEHarvest.dll  <-- Dynamic plugin (~50-180 KB)
│   │   ├── config.ini           <-- Radius=3.5, SameCrop=1, AdminOnly=0
│   │   └── modinfo.json
│   ├── Dream-Auto-Pickup/
│   │   ├── DreamAutoPickup.dll
│   │   ├── config.ini           <-- Radius=5.0, FastProximity=1, AdminOnly=0
│   │   └── modinfo.json
│   ├── XP-Share/
│   │   ├── XPShare.dll
│   │   ├── config.ini           <-- Enabled=1, Multiplier=0.35
│   │   └── modinfo.json
│   └── Admin-Control-Server/
│       ├── AdminControlServer.dll
│       └── admin_fishing_config.ini
```

---

## 2. Dynamic C-ABI Plugin Binding Interface (`dreamkurumiHT_plugin.h`)

### 2.1 Why Use the Dynamic Plugin Interface (~30-180KB Footprint)
Statically compiling the full framework into every individual mod DLL results in bloated binaries (2-4 MB each) and potential hook conflicts.
With `dreamkurumiHT_plugin.h`:
* Your mod DLL stays lightweight (**30 KB - 180 KB**).
* It dynamically resolves exports from `dreamkurumiHT.dll` at runtime via `GetProcAddress`.
* No external `.lib` files or link-time dependencies are required.
* Fully compatible with both C++20 and C# Native AOT.

### 2.2 PluginHost Structure and Exported Symbols
Initialize and access the framework through `dreamkurumi::PluginHost`:

```cpp
#include <dreamkurumiHT/dreamkurumiHT_plugin.h>

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID reserved) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hModule);
        
        auto& host = dreamkurumi::PluginHost::Instance();
        if (!host.Init()) {
            OutputDebugStringA("[MyMod] Failed to bind dreamkurumiHT.dll!\n");
            return TRUE;
        }

        host.Log(1, "[MyMod] Successfully bound to dreamkurumiHT Core Engine!");
    }
    return TRUE;
}
```

#### Core C-ABI Exported API Functions:
| Function | Description |
|---|---|
| `DK_ResolvePattern(name, sig, fallbackRva)` | Ultra-fast cached AOB pattern scanner |
| `DK_InlineHook(target, detour, ppOriginal)` | Near-memory 64KB trampoline detour hook |
| `DK_RegisterChainedMidHook(target, cb, prio, name)` | Priority-based chained mid-function hook |
| `DK_PatchBytes(target, bytes, count)` | Memory patch with auto-restoration |
| `DK_Nop(target, count)` | NOP assembly instructions safely |
| `DK_Log(level, msg)` | High-speed async double-buffered logging |
| `DK_Preset_SetGodMode(bool)` | Toggle invulnerability |
| `DK_Preset_SetInfiniteStamina(bool)` | Toggle infinite stamina |
| `DK_Preset_SetUnlimitedFlight(bool)` | Toggle unlimited glider flight |
| `DK_GetEntityPosition(pEnt, &x, &y, &z)` | Query 3D world position of an ECS entity |

---

## 3. Mod Manifest Specification (`modinfo.json` & `ModInfo.xml`)

The framework automatically parses metadata from your mod's directory:

### Format A: `modinfo.json` (Recommended ⭐)
```json
{
  "name": "DreamAoEHarvest",
  "displayName": "Dream AoE Crop Harvest",
  "version": "1.0.0",
  "author": "dreamkurumi",
  "description": "AoE farming crop harvesting with 3.5m radius and F8 hotkey toggle.",
  "website": "https://github.com/your-repo/enshrouded-mods"
}
```

### Format B: `ModInfo.xml` (7 Days to Die style)
```xml
<?xml version="1.0" encoding="utf-8"?>
<xml>
    <Name value="DreamAutoPickup" />
    <DisplayName value="Dream Auto Pickup Ground Loot" />
    <Version value="1.0.0" />
    <Author value="dreamkurumi" />
    <Description value="Ground loot, monster drops, logs, and ore auto pickup 5.0m." />
</xml>
```

---

## 4. Quickstart: Create Your First Mod in 15 Lines

### 4.1 Method A: Dynamic C-ABI Plugin (Recommended for Modular Mods)
```cpp
#include <windows.h>
#include <dreamkurumiHT/dreamkurumiHT_plugin.h>

typedef void (*FnTakeDamage)(void* character, float damage, uint32_t damageType);
FnTakeDamage g_pOriginalTakeDamage = nullptr;

void DetourTakeDamage(void* character, float damage, uint32_t damageType) {
    damage *= 0.5f; // Reduce incoming damage by 50%
    if (g_pOriginalTakeDamage) {
        g_pOriginalTakeDamage(character, damage, damageType);
    }
}

extern "C" __declspec(dllexport) int XhlNativePluginStart(void* ctx) {
    auto& host = dreamkurumi::PluginHost::Instance();
    if (!host.Init()) return 0;

    void* pTarget = host.ResolvePattern("TakeDamage", "40 53 48 83 EC 20 48 8B D9 0F 29", 0);
    if (pTarget) {
        host.InlineHook(pTarget, (void*)DetourTakeDamage, (void**)&g_pOriginalTakeDamage);
        host.Log(1, "[DamageMod] TakeDamage hook installed cleanly!");
    }
    return 1;
}

extern "C" __declspec(dllexport) void XhlNativePluginRequestStop() {
    // Hooks and resources are automatically cleaned up
}
```

---

### 4.2 Method B: Direct Header-Only Macro (`dreamkurumiHT.hpp`)
```cpp
#include <windows.h>
#include <dreamkurumiHT/dreamkurumiHT.hpp>

DREAM_HARMONY_AOB_AUTO(StaminaPatch, "StaminaDrain", "48 89 5C 24 ?? 57 48 83 EC", void, void* player, float amount) {
    static bool Prefix(void* player, float amount) {
        // Return false to skip the game's stamina reduction logic
        return false;
    }
};
DREAM_REGISTER_PATCH(StaminaPatch);

DREAM_MOD_ENTRY(
    /* OnStart */ {
        DREAM_LOG_INFO("Infinite Stamina Mod loaded!");
    },
    /* OnStop */ {
        DREAM_LOG_INFO("Infinite Stamina Mod unloaded.");
    }
)
```

---

## 5. Complete Harmony Hooking Guide

### 5.1 AOB Signature Scanning & Multi-Tier Caching
Hardcoded addresses and RVAs break when the game updates. `dreamkurumiHT` uses an AVX2/SSE-accelerated pattern scanner:

```cpp
void* addr = host.ResolvePattern(
    "CropInteractCheck", 
    "48 89 5C 24 10 48 89 74 24 18 57 48 83 EC 30 48 8B FA 48 8B D9", 
    0xCCD430
);
```
* **Tier 1 (Memory Cache):** On first run, discovered addresses are persisted to `mods/dreamkurumiHT/dreamkurumi_cache.ini`.
* **Tier 2 (Fast Launch):** Subsequent launches verify bytes and resolve instantly in **0.0001 ms**.
* **Tier 3 (Auto Invalidation):** When a game patch changes the binary's PE timestamp, cache is invalidated and scanned anew.

---

### 5.2 Inline Hooking with Near-Memory Trampoline Pool (64KB Arena)
On x64, a direct jump `E9 [disp32]` has a $\pm 2\text{ GB}$ range. Standard `VirtualAlloc` can allocate outside this range, requiring a bulky 14-byte indirect jump (`FF 25`).

**The Solution (`DreamPool.hpp`):**
* Allocates 64KB arena chunks in unused memory spaces within $\pm 2\text{ GB}$ of `enshrouded.exe`.
* Guarantees fast, compact 5-byte jumps for 100% stable detour trampolines.

---

### 5.3 Chained Mid-Hooking with Priority Scheduling (Zero Mod Conflict)
When multiple mods (e.g. AoE Harvest and Auto Pickup) need to hook into the exact same ground loot or interact instruction, standard hooks overwrite each other.

`dreamkurumiHT` solves this with **Chained Mid-Hooks**:
```cpp
// AoE Harvest registers with priority 100
host.RegisterChainedMidHook(lootTargetAddr, [](void* ctx) {
    auto cpu = (dreamkurumi::DreamCpuContext*)ctx;
    // Process crop gathering...
}, 100, "DreamAoEHarvest");

// Auto Pickup registers at the same point with priority 50
host.RegisterChainedMidHook(lootTargetAddr, [](void* ctx) {
    auto cpu = (dreamkurumi::DreamCpuContext*)ctx;
    // Process ground items...
}, 50, "DreamAutoPickup");
```
Both callbacks execute in order with zero conflicts!

---

### 5.4 Skipping Original Functions & Modifying Arguments/Returns

* **Skip Original Logic:** Return `false` in `Prefix`.
* **Execute Original Logic:** Return `true` in `Prefix`.

```cpp
DREAM_HARMONY_AOB_AUTO(FallDamagePatch, "FallDamage", "...", void, void* player, float height) {
    static bool Prefix(void* player, float height) {
        if (height > 50.0f) {
            DREAM_LOG_INFO("Lethal fall damage prevented!");
            return false; // Skip damage calculation
        }
        return true;
    }
};
```

---

### 5.5 Hardware Breakpoint VEH Hooking (Zero-Byte Code Modification)
For zero memory modification:
```cpp
auto& veh = dreamkurumi::DreamVeh::Instance();
veh.SetHardwareBreakpoint(targetAddress, 0, dreamkurumi::BreakpointCondition::Execute, [](PCONTEXT ctx) {
    DREAM_LOG_INFO("Hardware Breakpoint hit! RAX = 0x%llX", ctx->Rax);
});
```

---

### 5.6 Mid-Function Register Hooking (Access CPU Context: RAX, RCX, XMM)
```cpp
dreamkurumi::DreamMidHook midHook;
midHook.Install(targetInstructionAddress, [](dreamkurumi::DreamCpuContext* ctx) {
    uint64_t characterPtr = ctx->rcx;
    float currentHealth = ctx->GetXmmFloat(0);
    ctx->SetXmmFloat(0, currentHealth + 50.0f);
});
```

---

### 5.7 VTable & RTTI Hooking (Intercept C++ Class Virtual Methods)
```cpp
void** vtable = dreamkurumi::DreamRtti::FindVTable("keen::Character");
dreamkurumi::DreamVTableHook vHook;
vHook.Hook(vtable, 4, reinterpret_cast<void*>(MyCustomTakeDamage));
```

---

## 6. 100% Safe Memory Management & Pointer Chains

### 6.1 Sandbox Memory Safe Read/Write (Zero Access Violation Crashes)
In vanilla C++, dereferencing bad pointers triggers an instant Access Violation (`0xC0000005`) crash.
`DreamMemory` executes all operations inside the `DreamCrashGuard` VEH/SEH sandbox:

```cpp
using namespace dreamkurumi;

// Returns 0.0f safely instead of crashing if pHealth is null or unmapped
float hp = DreamMemory::Read<float>(pHealth, 0.0f);

// Safely writes memory with automatic VirtualProtect handling
bool success = DreamMemory::Write<float>(pHealth, 500.0f);
```

---

### 6.2 Multi-Level Pointer Chains (`ReadChain` / `WriteChain`)
Traverse nested pointer chains like Cheat Engine: `[[[pBase + 0x18] + 0x30] + 0x48]`:

```cpp
using namespace dreamkurumi;

float stamina = DreamMemory::ReadChain<float>(pPlayer, { 0x18, 0x30, 0x48 }, 100.0f);
DreamMemory::WriteChain<float>(pPlayer, { 0x18, 0x30, 0x48 }, 999.0f);
```

---

### 6.3 RAII Byte Patching & NOPs (`DreamBytePatch`)
```cpp
using namespace dreamkurumi;

{
    DreamBytePatch patch(codeAddress, "90 90 90 90 90");
    patch.Apply();
    // Game code is bypassed while inside scope...
} // Original code is restored automatically upon exit!
```

---

## 7. Asynchronous Double-Buffered Logging Engine (`DreamLog`)

### 7.1 Double-Buffer Queue Architecture & Background Worker (<1µs Overhead)
Direct disk I/O on the main game thread causes micro-stuttering. `DreamLog` features an asynchronous double-buffered queue:
* Calling `DREAM_LOG_*` completes in **< 1 microsecond**.
* Atomic pointer swap transfers logs to the processing queue every 10ms.
* Background worker writes entries to disk and memory ring buffers without stuttering the game.

### 7.2 Anti-Spam Deduplication Filter (2.0s Sliding Window)
Repeated messages generated across thousands of entities are automatically aggregated into `[Repeated N times]`.

### 7.3 Channel Verbosity & Timed Burst Debugging
Independent log channels (`Global`, `AoE`, `Loot`, `Admin`, `XP`, `Hook`, `Net`, `ECS`):
* Levels: `0=Off`, `1=Info`, `2=Warning`, `3=Trace`.
* **Timed Burst Mode:** Temporarily boost a channel to Trace for N seconds (e.g. `verbose aoe 3 30`), auto-decaying back to normal when finished.

### 7.4 In-Memory Ring Buffer & Live Grep / Tail Logs
Maintains the last 2,048 log entries in RAM for instant console querying (`tail`, `grep`).

---

## 8. Dynamic Runtime Configuration (`DreamLiveConfig`)

### 8.1 Registering Config Properties
Register live-tunable variables with optional change callbacks:

```cpp
#include <dreamkurumiHT/DreamLiveConfig.hpp>

auto& cfg = dreamkurumi::DreamLiveConfig::Instance();

cfg.RegisterFloat("aoe.radius", 3.5f, "AoE Crop Harvest Radius in meters", [](float newRadius) {
    g_harvestRadius = newRadius;
    DREAM_LOG_INFO("[DreamAoEHarvest] Live updated radius to %.2fm", newRadius);
});
```

### 8.2 Hot-Reload Callbacks: Real-time Memory Updates without Restart
Executing `set aoe.radius 4.5` updates the value in RAM and triggers the callback immediately—zero server or client restart needed!

---

## 9. Interactive Diagnostics Console & CLI Overlay (`DreamOverlay`)

### 9.1 Dual Mode: Windows GUI Console (`F10`) & Piped Stdin (Headless Daemon)
* **Client Mode:** Press **`F10`** to toggle the diagnostics console or **`F9`** for the ImGui overlay.
* **Server Mode:** Direct stdin listener supports CLI input via PowerShell, SSH, or automation tasks (`send_input`).

### 9.2 Console Command Reference Table & Short Aliases

| Command | Short Alias | Syntax | Description |
|---|---|---|---|
| `help` | `h` | `help` / `h` | Display all registered commands and aliases |
| `status` | `s` | `status` / `s` | Framework and server status summary |
| `perf` | `p` | `perf` / `p` | Memory Working Set, Peak, Free RAM, and VEH stats |
| `clear` | `c` | `clear` / `c` | Clear console screen |
| `get` | - | `get <key>` / `get all` | Inspect live runtime parameters |
| `set` | - | `set <key> <val>` | Update live parameter in memory (e.g. `set aoe.radius 4.5`) |
| `verbose` | `v` | `verbose <ch> <0-3> [sec]` | Set log verbosity and timed burst (e.g. `v aoe 3 30`) |
| `tail` / `log` | `t` | `tail [N]` / `t 10` | Print recent N logs in console |
| `grep` | - | `grep <pattern> [N]` | Search in-memory ring buffer for pattern |
| `players` | - | `players` | List active connected players and entity IDs |
| `snapshot` | - | `snapshot` | Export comprehensive diagnostic report (`snapshot_*.txt`) |
| `dump` | - | `dump <hex_addr> [len]` | Safe memory hex dump (e.g. `dump 0x7FF... 64`) |
| `sig` | - | `sig <pattern>` | Real-time AVX2/SSE pattern scan in game memory |
| `ecs` | - | `ecs <id>` | Inspect ECS entity components |
| `lua` | `l` | `lua <expression>` | Evaluate LuaJIT expression in memory |
| `luafile` | - | `luafile <path>` | Execute `.lua` script file from disk |

### 9.3 Holistic Diagnostic Snapshots (`snapshot`)
Generates an audit report (`snapshot_YYYYMMDD_HHMMSS.txt`) containing system performance, working sets, live config parameters, and recent in-memory logs with a single command.

---

## 10. Game Engine & ECS Service Locator (`DreamGameContext`)

### 10.1 Entity Component System (ECS) Inspection & 3D Coordinates
```cpp
auto& host = dreamkurumi::PluginHost::Instance();
void* pEntity = host.GetEntity(pGameContext, entityId);
if (pEntity) {
    float x = 0.0f, y = 0.0f, z = 0.0f;
    if (host.GetEntityPosition(pEntity, &x, &y, &z)) {
        DREAM_LOG_INFO("Entity %u Position: X=%.2f Y=%.2f Z=%.2f", entityId, x, y, z);
    }
}
```

### 10.2 Player Coordinate Tracking & Teleportation
```cpp
auto& game = dreamkurumi::DreamGame::Instance();
if (game.IsPlayerReady()) {
    DreamVector3 pos = game.GetPlayerPosition();
    game.TeleportPlayer(150.0f, 85.0f, -320.0f);
}
```

### 10.3 Built-in Gameplay Presets
```cpp
game.SetGodMode(true);             // Invulnerability
game.SetGhostMode(true);           // Invisibility to monsters
game.SetInfiniteStamina(true);     // Stamina never drains
game.SetInfiniteMana(true);        // Mana never drains
game.SetInfiniteShroud(true);      // Shroud timer never expires
game.SetFreeCraft(true);           // Craft without materials
game.SetUnlimitedFlight(true);     // Infinite glider distance
game.SetSuperMining(true, 5.0f);   // 5x mining speed
```

---

## 11. Cross-Platform Dedicated Server & Client Separation

### 11.1 Platform Macros (`DREAM_SERVER_ONLY` / `DREAM_CLIENT_ONLY`)
```cpp
DREAM_SERVER_ONLY({
    DREAM_LOG_INFO("Dedicated Server Mode Active");
});

DREAM_CLIENT_ONLY({
    DREAM_LOG_INFO("Client GUI Mode Active");
});
```

### 11.2 Network RPC Bridge (`DreamNetBridge`)
```cpp
// Client sends RPC request
dreamkurumi::NetBufferWriter writer;
writer.WriteString("RequestSpeedBuff");
dreamkurumi::DreamNetBridge::Instance().SendToServer("BuffChannel"_hash, 0x101, writer);

// Server receives RPC
dreamkurumi::DreamNetBridge::Instance().RegisterHandler("BuffChannel"_hash, 0x101, [](const auto& pkt) {
    dreamkurumi::NetBufferReader reader(pkt.payload);
    std::string buffName = reader.ReadString();
    DREAM_LOG_INFO("Player %u requested: %s", pkt.senderId, buffName.c_str());
});
```

### 11.3 Server-to-Client Config Broadcasting (`DreamServerSync`)
```cpp
dreamkurumi::DreamServerSync::Instance().SetSyncVar("HarvestMultiplier", 2.0f);
dreamkurumi::DreamServerSync::Instance().BroadcastServerSettings();
```

---

## 12. C# .NET 8/10 Developer Guide (Native AOT)

Compile ultra-fast C# mods into native x64 DLLs without CLR dependencies:

```csharp
using System;
using System.Runtime.InteropServices;
using DreamKurumi;

namespace MyDreamModCSharp
{
    public static unsafe class ModMain
    {
        [UnmanagedCallersOnly(EntryPoint = "XhlNativePluginStart")]
        public static int XhlNativePluginStart(IntPtr ctx)
        {
            DreamEngine.DK_Log(1, "[C# Mod] Started via dreamkurumiHT Native AOT!");

            if (DreamEngine.DK_Game_IsPlayerReady())
            {
                DreamEngine.DK_Game_SetGodMode(true);
                DreamEngine.DK_Game_TeleportPlayer(150f, 90f, 200f);
            }

            return 1;
        }

        [UnmanagedCallersOnly(EntryPoint = "XhlNativePluginRequestStop")]
        public static void XhlNativePluginRequestStop()
        {
            DreamEngine.DK_UnpatchAll();
        }
    }
}
```

---

## 13. High-Speed JSON Engine (DreamJson Powered by yyjson)

```cpp
#include <dreamkurumiHT/dreamkurumiHT.hpp>

// Parse JSON in gigabytes-per-second
std::string jsonStr = R"({"radius": 5.0, "enabled": true, "targets": ["stone", "wood"]})";
auto doc = dreamkurumi::DreamJson::Parse(jsonStr);
if (doc.is_valid()) {
    double radius = doc["radius"].as_double(3.5);
    bool enabled  = doc["enabled"].as_bool(true);
}
```

---

## 14. In-Memory Scripting Engine (DreamLua Powered by LuaJIT 2.1)

```cpp
auto& lua = dreamkurumi::DreamLuaEngine::Instance();
lua.Initialize();

lua.ExecuteString(R"(
    dream.log("Hello from LuaJIT inside Enshrouded!")
    if dream.is_player_ready() then
        dream.set_god_mode(true)
        dream.teleport(200, 100, 300)
    end
)");
```

---

## 15. Automated Build & Sync Engine (`dev_sync.ps1`)

### 15.1 One-Click Multi-Target Build & Deploy
```powershell
# Build and sync all components (Core, Server Loader, Server Mods, Client Mods)
powershell -ExecutionPolicy Bypass -File tools/dev_sync.ps1 -Target All

# Build Core Engine only
powershell -ExecutionPolicy Bypass -File tools/dev_sync.ps1 -Target Core

# Build Server Mods only
powershell -ExecutionPolicy Bypass -File tools/dev_sync.ps1 -Target ServerMods
```

### 15.2 Hot-Replacement Trick for In-Use DLLs (`.locked_*`)
Windows locks active DLLs while `enshrouded.exe` is running. `dev_sync.ps1` safely renames locked DLLs to `.locked_<timestamp>`, allowing in-place hot rebuilding with 0 permission errors!

---

## 16. Best Practices, Performance Guarantees & Troubleshooting

1. **Avoid Disk I/O & Mutex Contention in Hot Paths:**
   * Never call `std::cout`, `printf`, or file streams inside per-frame or harvest loops. Use `host.Log()` for sub-microsecond async logging.
2. **Strictly Abide by Rule 1 & Rule 2:**
   * Keep mods completely decoupled from the core framework.
   * Default `AdminOnly` to `false` for QoL mods so all players on dedicated servers enjoy enhanced gameplay.
3. **Guard Memory Access with `DreamMemory`:**
   * Avoid raw pointer dereferencing (`*p = 10;`). Use `DreamMemory::Read<T>()` and `DreamMemory::Write<T>()` for 100% crash protection.
4. **Use Multi-Tier AOB Signatures:**
   * Include wildcards `??` for displacement bytes to ensure survivability across Enshrouded game updates.

---
*Crafted for peak performance, zero crashes, and seamless mod development — dreamkurumiHT Team*
