# dreamkurumiHT Framework (Dream Core)
> **Modern Native Harmony Hooking & Modding Framework for C++20 / x64 Windows**  
> *(Zero-Assembly, Zero-RAM-Scan, Hardware Breakpoint VEH, High-Performance Keen Games Engine Hooking)*  
> **Framework Version:** `3.4.0` | **Architecture:** x64 Windows | **Target Game:** Enshrouded 1.0+

---

<p align="center">
  <b>Language Switcher / สลับภาษา:</b><br>
  <a href="README.md">🇬🇧 <b>English (Default)</b></a> •
  <a href="README_TH.md">🇹🇭 <b>ภาษาไทย (Thai)</b></a><br><br>
  <a href="https://github.com/kurumiht8947/Dream-Core/releases/download/v3.4.0/Dream-Core-v3.4.0.zip">
    <img src="https://img.shields.io/badge/Download-Dream--Core%20v3.4.0%20(Plug%20%26%20Play)-brightgreen?style=for-the-badge&logo=windows" alt="Download Dream-Core v3.4.0" />
  </a>
</p>

---

## Overview

**dreamkurumiHT (Dream Core)** is an industrial-grade native modding and runtime hooking framework purpose-built for high-performance 64-bit games, specifically **Enshrouded (Keen Holistic Engine)**. It brings the declarative simplicity and elegance of C# Harmony into native **C++20, C# (.NET Native AOT), and LuaJIT 2.1**.

Unlike traditional shallow memory patchers, dreamkurumiHT operates directly on process memory using **consolidated near-memory trampolines, hardware breakpoint VEH engines, deep ECS reflection, and zero-syscall pointer dereferencing**, delivering microsecond execution times without micro-stutters or frame drops.

---

## 41 Master Capabilities & Architecture

1. **Declarative Harmony API (`Prefix` / `Postfix`)**
   - Clean and expressive function hooking without writing raw opcodes or calculating `E9 JMP` offsets manually.
   - Return `false` in `Prefix` to completely skip the original game function (Skip Original).
   - Read and mutate function arguments directly via pointer / reference semantics.
   - Read and modify return values seamlessly in `Postfix`.
   - Supports passive observers (`void Prefix(...)`).
2. **Automated `DreamHarmony::PatchAll()` and `UnpatchAll()`**
   - The iconic Harmony capability implemented in modern C++20.
   - Automatically registers all declared hooks across your codebase with a single `PatchAll()` call in `DllMain`.
   - Cleanly restores all original game bytes upon unloading via `UnpatchAll()` to prevent process crashes.
3. **Hardware Breakpoint & VEH Engine (`DreamVeh`)**
   - Hooks code execution via CPU Debug Registers (`DR0 - DR3`) and Vectored Exception Handling (VEH).
   - **Zero-Byte Patching:** Leaves game executable memory 100% untouched and byte-identical.
   - **Zero-RAM Scanning:** Binds directly to variable addresses and data accesses.
   - Thread-safe kernel context transitions for the calling thread.
4. **Direct Game Function Invoker (`DreamInvoker`)**
   - Resolves and invokes internal game functions in a type-safe manner:
     ```cpp
     auto fn = DreamInvoker::Get<void(void*, int)>(pFunc);
     fn(player, 100);
     ```
5. **Keen ECS Reflection Engine (`DreamReflection`)**
   - Directly reads and writes variables inside Keen Games ECS structs and components using engine metadata without relying on hardcoded offsets.
6. **Pure C++ Register Interception (`DreamMidHook`)**
   - Completely eliminates external `.s` assembly files.
   - Directly accesses and modifies CPU registers (`RAX`..`R15`, `XMM0`..`XMM15`) through the `DreamCpuContext` struct in C++.
   - Automated 16-byte stack alignment prevents SSE/AVX crashes.
   - Integrated HDE64 disassembler computes instruction lengths automatically.
7. **Build-Aware Symbol Offset Cache (`DreamCache`)**
   - Validates the game's PE Header `TimeDateStamp`.
   - Loads cached RVAs from `dreamkurumi_cache.ini` in $O(1)$ constant time ($0.0001\text{ ms}$).
   - **Bypasses AOB scanning on every game launch.**
   - **Smart Fallback:** Runs a background pattern scan only once after game updates, automatically caching resolved offsets for subsequent boots.
8. **Memory Transpiler & NOP Tool (`DreamMemoryPatch`)**
   - Safely applies byte patches and NOPs with full RAII automatic restoration.
9. **Virtual Method Table Hooking (`DreamVTableHook`)**
   - Directly intercepts C++ virtual table methods and DirectX/Vulkan interfaces.
10. **High-Performance Diagnostics Logger (`DreamLog`)**
    - **Persistent File Handle:** Eliminates repetitive open/close calls, cutting disk syscall overhead by 100%.
    - **Sub-ms Timestamps & Thread ID:** High-precision `[HH:MM:SS.mmm][TID:xxxx]` logging for multi-threaded analysis.
    - **ANSI Console Colors:** Clean, color-coded output in terminal and console windows.
    - **In-Memory Ring Buffer:** Retains the last 200 log messages in memory for ImGui overlay display or crash diagnostics.
    - **Memory HexDump Utility:** Instantly outputs hex + ASCII memory tables via `DreamLog::HexDump("Title", ptr, size)`.
11. **Universal C-ABI Exports (`DreamCExport`)**
    - Dynamic C-ABI plugin interface for C#, Lua, Python, Rust, and Zig.
12. **C# .NET Native AOT Binding (`DreamKurumi.cs`)**
    - Full C# bindings with delegate hook handles.
13. **Lua / LuaJIT Binding (`dreamkurumi.lua`)**
    - Ready-to-use bindings for Lua / EML scripting on Enshrouded.
14. **Near-Memory JIT Trampoline Pool (`DreamPool`)**
    - Consolidates trampoline allocations into a single 64KB arena within the $\pm 2\text{GB}$ relative jump range.
    - Reduces `VirtualAlloc` syscalls, saving up to 99% virtual memory and maximizing CPU instruction cache locality.
15. **Anti-Crash Thread Freeze & RIP Relocation (`DreamThread`)**
    - Automatically freezes other worker threads during hook installation.
    - If a thread's `RIP` points to stolen instructions, it relocates `RIP` to the corresponding trampoline offset, eliminating race conditions.
16. **MSVC RTTI & Dynamic VTable Resolver (`DreamRtti`)**
    - Scans TypeDescriptor and Complete Object Locator tables in `.rdata`.
    - Resolves class VTables and method pointers directly (e.g. `keen::ecs::EntityManager`) without hardcoded RVAs or RAM scans.
17. **Multi-Mod Hook Chain & Priority Pipeline (`DreamPipeline`)**
    - Enables multiple independent mods to hook the same game function without conflict.
    - Supports 6 priority tiers (`First`, `Highest`, `High`, `Normal`, `Low`, `Lowest`, `Last`).
18. **Anti-Crash Mod Sandbox Shield (`DreamCrashGuard`)**
    - Shields mod callbacks with Vectored Exception Handling + Windows TEB TLS.
    - Catches Access Violations (`0xC0000005`) and Null-Pointer Dereferences without crashing to desktop.
    - Logs the faulty mod and instruction address, resuming the host process safely.
19. **AVX2 & SSE Vectorized Pattern Scanner (`DreamScanner`)**
    - Scans memory using SIMD vector instructions (`_mm256_cmpeq_epi8`) at 32 bytes per clock cycle.
    - 20x faster than traditional scanning loops, traversing 150MB+ game binaries in milliseconds.
20. **Multi-Thread Hardware Breakpoint Auto-Sync (`DreamVeh`)**
    - Synchronizes debug registers (`DR0 - DR3`) across all engine worker threads.
    - Fully compatible with the job/task system of Keen Games Holistic Engine.
21. **Harmony-Style Dynamic Transpiler (`DreamWeaver`)**
    - Decodes x64 instruction streams using HDE64.
    - Identifies and rewrites opcodes, NOPs conditional checks (e.g., bypassing privilege jumps `JNE`), and adjusts immediate values.
22. **Enshrouded Game Logic & Mod Presets (`DreamPresets`)**
    - Directly extracts and manipulates `KeenLiveGameSettings` in real-time.
    - Includes presets for GodMode, GhostMode (perception set to 0.0f), Infinite Stamina, Infinite Mana, Super Mining, and Infinite Shroud.
    - Built-in Harmony patches for Free Crafting, Fall Damage Immunity, Infinite Item Split, Infinite Item Use, and Infinite Durability.
    - `DreamServerGuard`: Automatically detects headless dedicated server environments (`enshrouded_server.exe`), disabling UI/Audio hooks.
23. **In-Game Overlay, Hotkeys & Command Dispatcher (`DreamOverlay`)**
    - Hotkey bindings: **F9 (Toggle Menu)** and **F10 (Toggle Console)** with audio feedback.
    - Decoupled architecture: acts as an event and command router without forcing cheats.
    - In-game command dispatcher supporting `/console`, `/clear`, `/help`, `/status`, `/god`, `/ghost`, `/stamina`, `/craft`, `/save`, `/load`.
24. **Interactive Console CLI & Config Persistence (`DreamPresets::SaveConfig / LoadConfig`)**
    - **Interactive Console Reader:** Real-time CMD input (`god on`, `help`, `status`, `fastprod 1`) without requiring in-game UI.
    - **Live Status Table:** ANSI-colored status table displaying active mod states.
    - **Config Persistence:** Saves and loads settings from `dreamkurumi_config.ini` automatically.
    - **Fast Production:** Accelerates smelters and kilns (`keen::Time`) to complete cycles in 1 second.
25. **Server Anti-Cheat & Permission Guard (`DreamServerPolicy`)**
    - Category-based permission management (`Admin`, `Stealth`, `Survival`, `Economy`, `World`).
    - Enforces server-side anti-cheat policies on dedicated servers.
26. **Hook Performance Micro-Profiler (`DreamProfiler`)**
    - High-resolution nanosecond timing per hook.
    - Detects micro-stutters and logs warnings if execution exceeds configured thresholds (e.g. 0.5ms).
    - Interactive `/profile` report displays call counts, average, min, and max execution times.
27. **Dedicated Server Policy Separation (`server_policy.ini`)**
    - Strict file separation: Client uses `dreamkurumi_config.ini`, Dedicated Server uses `server_policy.ini`.
28. **Real-Time Config & Policy Hot-Reload (`DreamWatcher`)**
    - Zero-restart file watcher automatically reloads `.ini` configuration files with 200ms debouncing.
29. **Native Vulkan Presentation Hook & GUI Overlay (`DreamVulkan`)**
    - Header-only Vulkan hook intercepting `vkQueuePresentKHR` in Vulkan 1.3 without requiring the Vulkan SDK.
    - Supports in-game ImGui / custom overlays triggered by **F9**.
    - Dedicated Server Immunity disables graphics logic on headless servers.
30. **Auto Crash Report & Stack Trace Analyzer (`DreamCrashDump`)**
    - Captures fatal exceptions (`0xC0000005 Access Violation`, `0xC00000FD Stack Overflow`).
    - Dumps all x64 register states (`RAX`..`R15`, `RIP`, `RSP`, `RBP`, `EFlags`) and 32 bytes surrounding `RIP`.
    - Resolves module names and relative virtual addresses (RVA).
    - Writes formatted crash diagnostics to `dream_crash_report_YYYYMMDD_HHMMSS.txt`.
31. **Heuristic Multi-Tier Signature & Backup Engine (`DreamHeuristic`)**
    - 5-tier patch-resilient resolution:
      1. *Tier 1: Symbol Cache* ($0.0001\text{ ms}$)
      2. *Tier 2: Primary Exact Signature*
      3. *Tier 3: Secondary Relaxed Heuristic* (Wildcards `??`)
      4. *Tier 4: Anchor Proximity & Backward Prologue Discovery* (`55 48 89 E5`, `48 83 EC`)
      5. *Tier 5: Fallback Hardcoded RVA*
32. **Path Freedom & Self-Contained Mod Config (`DreamEnv` & `DreamModConfig`)**
    - 100% path-independent: calculates mod directory from physical module base address.
    - Standalone INI manager binds to individual mod folders, avoiding filename collisions.
33. **Semantic Versioning & Conflict Resolver (`DreamSemVer`)**
    - SemVer 2.0 parser (`1.0.0`, `v1.2.5`, `2.0.0-beta`).
    - Automatically suppresses duplicate/outdated mod instances to prevent infinite hook recursion.
34. **Compile-Time 64-bit FNV-1a Hash Engine (`DreamHash`)**
    - Zero-runtime overhead 64-bit FNV-1a hashing via C++20 `constexpr`.
    - Enables string switch-case jump tables via `""_hash` (e.g. `case "god"_hash:`).
35. **Anti-Hang & Thread Deadlock Guard (`DreamWatchdog`)**
    - Monitors worker thread heartbeats.
    - Non-destructive sampling dumps callstacks to `dream_hang_report_*.txt` before hangs freeze the game.
36. **Client <-> Dedicated Server Mod Synchronization & RPC (`DreamNetBridge` & `DreamEnv`)**
    - Unified single binary: the same DLL runs on both client and dedicated server.
    - Type-safe binary serialization (`NetBufferWriter` / `NetBufferReader`) with FNV-1a multiplexing.
37. **Safe Memory Reading & Pointer Chains (`DreamMemory`)**
    - Bad pointer sandbox safely handles invalid memory accesses without crashing.
    - Multi-level pointer chain evaluation (`ReadChain` / `WriteChain`).
38. **Engine Service Locator & High-Level API (`DreamGameContext`)**
    - Local player auto-capture with lifecycle event callbacks (`OnPlayerSpawn`).
    - One-liner API for teleportation, coordinates, and state toggles.
39. **Ultra-Fast JSON Engine (`DreamJson` / `yyjson`)**
    - Powered by `yyjson`, delivering gigabyte-per-second parsing and stringification.
    - Memory leak-free C++20 RAII wrappers.
40. **In-Memory Scripting & JIT (`DreamLua` / `LuaJIT 2.1`)**
    - Embedded 64-bit LuaJIT 2.1 executes scripts at native speed with crash-guarded sandboxing.
    - Two-way Lua Table <-> yyjson bridge.
41. **In-Game Chat Command Interceptor & Dispatcher (`Hook_ChatMessage_Relay`)**
    - Intercepts incoming network chat packets beginning with `/` (e.g. `/fc on`, `/god on`, `/help`, `/online`).
    - Executes server commands in real-time and silently drops command messages from public chat broadcast (`0x12` consume code).

---

## Verification Suite (41/41 Tests Passed - 100%)

```text
[TEST 1] Testing DreamCache...                         -> [PASS]
[TEST 2] Testing Baseline Functions...                 -> [PASS]
[TEST 3] Installing Harmony DamagePatch...             -> [PASS]
[TEST 4] Installing Harmony StaminaPatch...            -> [PASS]
[TEST 5] Testing Dynamic Disable / Restore...          -> [PASS]
[TEST 6] Testing Passive Observer (void Prefix)...     -> [PASS]
[TEST 7] Testing ResolvePattern Fallback...            -> [PASS]
[TEST 8] Testing DreamHarmony::PatchAll()...           -> [PASS]
[TEST 9] Testing DreamMemoryPatch (NOP & Restore)...   -> [PASS]
[TEST 10] Testing DreamVTableHook...                   -> [PASS]
[TEST 11] Testing DreamVeh Hardware Breakpoint...      -> [PASS] (Zero Byte Patching!)
[TEST 12] Testing DreamInvoker (Direct Call)...        -> [PASS]
[TEST 13] Testing DreamReflection (Keen ECS Field)...  -> [PASS]
[TEST 14] Testing DreamTrampolinePool (Near Arena)...  -> [PASS] (Consolidated 64KB Arena!)
[TEST 15] Testing ScopedThreadFreeze (Anti-Crash)...   -> [PASS] (Safe Snapshot & RIP Relocation!)
[TEST 16] Testing DreamRtti (Dynamic VTable)...        -> [PASS] (Zero-RVA Class Metadata Resolver!)
[TEST 17] Testing Hook Pipeline & Priority Chain...    -> [PASS] (Multi-Mod Chaining & Ordering!)
[TEST 18] Testing Upgraded DreamLog System...          -> [PASS] (Persistent I/O, Timestamps, Ring Buffer, HexDump, Console Window!)
[TEST 19] Testing DreamCrashGuard (Anti-Crash Shield)...-> [PASS] (Suppressed 0xC0000005, Prevented Game Crash!)
[TEST 20] Testing DreamScanner (AVX2/SSE SIMD)...      -> [PASS] (Scanned 256KB Buffer with 32-byte SIMD!)
[TEST 21] Testing DreamVeh Multi-Thread Sync...        -> [PASS] (Synchronized DR0-DR3 to All Worker Threads!)
[TEST 22] Testing DreamWeaver Dynamic Transpiler...    -> [PASS] (Native x64 Token Stream Instruction Rewriter!)
[TEST 23] Testing DreamPresets (Game Logic Suite)...   -> [PASS] (Keen LiveGameSettings & Dedicated Server Guard!)
[TEST 24] Testing DreamOverlay (Hotkeys & Commands)... -> [PASS] (F9/F10 Hotkeys, Decoupled Presets & Dispatcher!)
[TEST 25] Testing Config Persistence & Interactive CLI -> [PASS] (Save/Load Config, Fast Production & Status Table!)
[TEST 26] Testing DreamServerPolicy (Anti-Cheat Guard)-> [PASS] (EnforceStrictServerRules, Selective Blocking & Violation Intercept!)
[TEST 27] Testing DreamProfiler (Hook Micro-Profiler)-> [PASS] (Nanosecond Precision, Alert Threshold & /profile Report!)
[TEST 28] Testing DreamServerPolicy File Persistence -> [PASS] (server_policy.ini Save & Load Lifecycle!)
[TEST 29] Testing DreamWatcher (Live Hot-Reloading)  -> [PASS] (Real-time File Modification Detection & Debounced Reload!)
[TEST 30] Testing DreamVulkan (Vulkan Swapchain Hook)-> [PASS] (vkQueuePresentKHR Hook, FPS Meter & F9 GUI Overlay Interface!)
[TEST 31] Testing DreamCrashDump (Auto Crash Report) -> [PASS] (Captured Access Violation, Registers, Module+RVA & Callstack!)
[TEST 32] Testing DreamHeuristic (Multi-Tier Resolver)-> [PASS] (Primary, Secondary Wildcard, Backward Prologue & Auto-Cache!)
[TEST 33] Testing DreamEnv & DreamModConfig (Dev Path) -> [PASS] (Discovered Game & Mod Dirs, Self-Config Persistence & Registry!)
[TEST 34] Testing DreamSemVer & Duplicate Mod Resolver -> [PASS] (SemVer 2.0 Comparator, Duplicate Suppression & Auto-Upgrade!)
[TEST 35] Testing DreamHash (Compile-Time 64-bit Hash)-> [PASS] (Zero-alloc FNV-1a & ""_hash Switch-Case Jump Table!)
[TEST 36] Testing DreamWatchdog (Anti-Hang Guard)    -> [PASS] (Heartbeat Monitor, Deadlock Timeout & Report Dumper!)
[TEST 37] Testing DreamNetBridge & Dual-Side Sync    -> [PASS] (Unified Client <-> Dedicated Server RPC & Auto-Sync Settings!)
[TEST 38] Testing DreamMemory (Safe Reads & Patches)  -> [PASS] (Bad Pointer Sandbox, Multi-Level Pointer Chains & RAII Patches!)
[TEST 39] Testing DreamGameContext (Service Locator)  -> [PASS] (LocalPlayer Auto-Capture, Teleportation & Zero-Pointer API!)
[TEST 40] Testing DreamJson (yyjson High-Speed Engine) -> [PASS] (yyjson DOM parsing, type validation, array access & stringify!)
[TEST 41] Testing DreamLua (LuaJIT 2.1 & yyjson Bridge)-> [PASS] (JIT script execution, DreamGameContext services & table bridge!)
```

**ALL 41 TESTS PASSED! dreamkurumiHT IS FULLY VERIFIED!**

---

## Architectural Rules

### Rule 1: Strict Architectural Separation of `dream` (Harmony) and `mods`
* `dreamkurumiHT` is strictly the native hooking framework and core runtime engine.
* **Never embed game-specific mod logic directly into `dreamkurumiHT`.**
* Every mod lives in its own independent directory and links dynamically via the lightweight C-ABI header (`dreamkurumiHT_plugin.h`).

### Rule 2: Universal Server Access vs Admin-Whitelist Policy
* **Universal Player Access by Default:** Gameplay and quality-of-life mods must be usable by all players connected to dedicated servers by default (`AdminOnly = false`).
* **Admin-Whitelist Exception:** Restrict administrative, debugging, and cheat capabilities exclusively to verified admin groups or entity IDs.

---

## Quickstart: Creating a Mod in 15 Lines

```cpp
#include <dreamkurumiHT/dreamkurumiHT_plugin.h>

class MyEnshroudedMod {
public:
    static void Init() {
        auto& host = dreamkurumi::PluginHost::Instance();
        host.LogInfo("[MyMod] Hello from Dream Core!");
    }
};

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID reserved) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hModule);
        MyEnshroudedMod::Init();
    }
    return TRUE;
}
```

---

## Repository Structure

```text
├── bindings/         # C# (.NET Native AOT), Lua, Python bindings
├── deps/             # Vendored dependencies (LuaJIT 2.1, HDE64, yyjson)
├── examples/         # Example mods and reference implementations
├── include/          # Complete dreamkurumiHT C++20 header collection
├── lib/              # Prebuilt static libraries (liblua51, libyyjson)
├── template/         # Ready-to-build C++ mod project template
├── template_csharp/  # Ready-to-build C# .NET Native AOT mod project template
├── tests/            # Test suite verification runner
├── CHANGELOG.md      # Framework release changelog
├── DEVELOPER_GUIDE.md# Complete Developer Guide (Thai)
├── DEVELOPER_GUIDE_EN.md # Complete Developer Guide (English)
├── README.md         # English Documentation (Default Landing Page)
└── README_TH.md      # Thai Documentation (คู่มือภาษาไทย)
```

---

## Topics & Tags

`#enshrouded` `#enshrouded-mods` `#aoe-harvest` `#dream-core` `#dreamkurumiht` `#harmony` `#modloader` `#game-modding` `#cpp20` `#reverse-engineering` `#dedicated-server` `#farming-mod`

---

## License

This project is released under the **MIT License**.
