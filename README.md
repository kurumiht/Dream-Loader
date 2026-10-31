# dreamkurumiHT Framework (Dream Core)
> **Modern Native Harmony Hooking & Modding Framework for C++20 / x64 Windows**  
> *(Zero-Assembly, Zero-RAM-Scan, Hardware Breakpoint VEH, High-Performance Keen Games Engine Hooking)*  
> **Framework Version:** `3.8.4` | **Architecture:** x64 Windows | **Target Game:** Enshrouded 1.0+

---

<p align="center">
  <b>Language Switcher / สลับภาษา:</b><br>
  <a href="README.md">🇬🇧 <b>English (Default)</b></a> •
  <a href="README_TH.md">🇹🇭 <b>ภาษาไทย (Thai)</b></a><br><br>
  <a href="https://github.com/kurumiht/Dream-Loader/releases/download/v3.8.4/Dream-Core-v3.8.4.zip">
    <img src="https://img.shields.io/badge/Download-Dream--Core%20v3.8.4%20(Framework)-brightgreen?style=for-the-badge&logo=windows" alt="Download Dream-Core v3.8.4 Framework" />
  </a>
  &nbsp;&nbsp;
  <a href="https://github.com/kurumiht/Dream-Loader/releases/download/v3.8.4/Dream-AoE-Harvest-v1.4.0.zip">
    <img src="https://img.shields.io/badge/Download-Dream--AoE--Harvest%20v1.4.0%20(Mod)-blue?style=for-the-badge&logo=windows" alt="Download Dream-AoE-Harvest Mod" />
  </a>
  &nbsp;&nbsp;
  <a href="https://github.com/kurumiht/Dream-Loader/releases/download/v3.8.4/Enshrouded-v1.0.0.3-Build-1273043-SDK-Dump.zip">
    <img src="https://img.shields.io/badge/Download-Enshrouded%20SDK%20Dump%20(Build%201273043)-orange?style=for-the-badge&logo=code" alt="Download Enshrouded SDK Dump" />
  </a>
</p>

---

## Overview

> [!NOTE]
> ### 💬 Author's Note & Purpose
> **Why this project was made:**  
> I originally developed this framework and mod for use on my own dedicated server and in my own gameplay. I'm sharing it openly with the modding community in case anyone else wants to use it, learn from it, or build their own mods with it!  
>
> **Separate Downloads (`Dream-Core` vs `Dream-AoE-Harvest`):**  
> To avoid any confusion—especially if you already use or develop other mods similar to ours—the downloads are provided in two separate packages:  
> * 📦 **`Dream-Core-v3.8.4.zip`**: The standalone core native Harmony framework engine and C-ABI plugin loader. Does not bundle gameplay mods.  
> * 🌾 **`Dream-AoE-Harvest-v1.4.0.zip`**: The standalone dedicated AoE crop harvesting mod plugin (v1.4.0, 3.5m radius, F8 toggle), provided separately with chest/container protection so you can freely plug it into your server or client `mods/` directory.  
* 📚 **`Enshrouded-v1.0.0.3-Build-1273043-SDK-Dump.zip`**: The standalone reverse engineering engine dump (12,646 classes, 38,367 fields, 72,746 functions) for Game Build 1273043, provided for mod developers building custom plugins.

---

**dreamkurumiHT (Dream Core)** is an industrial-grade native modding and runtime hooking framework purpose-built for high-performance 64-bit games, specifically **Enshrouded (Keen Holistic Engine)**. It brings the declarative simplicity and elegance of C# Harmony into native **C++20, C# (.NET Native AOT), and LuaJIT 2.1**.

Unlike traditional shallow memory patchers, dreamkurumiHT operates directly on process memory using **consolidated near-memory trampolines, hardware breakpoint VEH engines, deep ECS reflection, and zero-syscall pointer dereferencing**, delivering microsecond execution times without micro-stutters or frame drops.

---

## 74 Master Capabilities & Architecture

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
18. **Anti-Crash Mod Sandbox Shield (`DreamCrashGuard` v3.9.0 Hardened Edition)**
    - Shields mod callbacks with Vectored Exception Handling + direct `RtlCaptureContext` / `NtContinue` context restoration (zero CRT `longjmp`, rock-solid across `-O0` and `-O2`).
    - **Kernel-Safe Memory Probing:** Validates user address space boundaries and thread TEB stack expansion zones (`StackLimit` vs `DeallocationStack`) upfront. Memory probes strictly pass through `STATUS_GUARD_PAGE_VIOLATION (0x80000001)` to prevent stripping `PAGE_GUARD` bits.
    - Catches Access Violations (`0xC0000005`), #GP, or Null-Pointer Dereferences without crashing to desktop.
    - **Zero-Allocation & Zero-Lock VEH:** Captures all CPU registers (`RAX-R11`, `RSP`, `RBP`) and traces up to 12 stack frames bounded strictly by thread TEB limits.
    - **Bounded Heuristic Leaf Unwind:** Scans up to 8 stack slots with IsLikelyReturnAddress() (x64 CALL opcode verification + PE image boundary checks) to accurately unwind leaf functions and trampoline stolen prologues.
    - **Debugger Pass-Through:** Configurable SetDebuggerPassThrough() / DREAM_DEBUG_PASSTHROUGH=1 allowing attached Visual Studio / x64dbg debuggers to break on mod crashes while strictly safeguarding DreamMemory memory probes.
    - **Advanced Root Cause Diagnostics:** Automatically identifies fault causes (Null Pointer, SSE unaligned #GP, Use-After-Free `0xDDDDDDDD` / `0xFEEEFEEE`, Uninitialized Memory `0xCDCDCDCD` / `0xCCCCCCCC`).
    - **Mod Call Site Detection:** If the crash occurs inside the game engine (`enshrouded.exe`), automatically identifies the calling frame inside the mod DLL with `addr2line` guidance.
    - **Circuit Breaker & Hot-Reload Reset:** Automatically quarantines broken mods after 5 consecutive crashes, and resets cleanly upon file recompilation/hot-reload.
    - **In-Game Toast Alert:** Bridges to `DreamToast` for an instant unobtrusive on-screen red warning banner (auto-suppressed on headless servers).
    - **Zero-Freeze Epilogue-Safe Hot-Reload Draining (`DreamPluginGate`):** Host-owned execution gate using `FlushProcessWriteBuffers()` asymmetric IPI memory barriers, preventing crashes in function epilogues (`RET`) upon module unloading.
    - **GameNetworkingSockets Boundary Demultiplexing (`DreamGnsTransportHook`):** Intercepts mod RPC datagrams (`0xD7EA0002` / `0xD7EA0001`) out-of-band at the network transport boundary before `keen::net` reads them, preventing bitstream deserialization desync.
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
42. **Multi-Mod Centralized Detour Dispatcher (`DreamDispatcher` / `DreamHarmony`)**
    - Collision-proof inline hook dispatcher allowing multiple independent mods to hook the exact same target function simultaneously.
    - Deterministic execution pipeline ordered by `HookPriority` (`First`, `Highest`, `High`, `Normal`, `Low`, `Lowest`, `Last`).
    - Hot-reload isolation: unhooking one mod cleanly leaves other hooked mods functioning seamlessly.
43. **Fuzzy Instruction-Aware AOB Scanner (`DreamFuzzyScanner` / `DreamHeuristic`)**
    - HDE64 machine code disassembly engine that analyzes instruction semantics across Keen Games patches.
    - Automatically wildcards shifting stack displacements (`[rsp+0x10]` vs `[rsp+0x18]`), stack frame allocations (`sub rsp, 0x20` vs `0x30`), and relative call/jump offsets.
44. **Single-Header C++ Mod SDK (`dream.hpp` / `dreamkurumi.hpp`)**
    - Single `#include <dream.hpp>` drop-in SDK for modders with `DREAM_PLUGIN`, `DREAM_PATCH`, and safe memory sandboxing.
    - **Hardware DWM Layered Click-Through Toast Engine (`DreamToast` / `dream::ShowToast`):** Fast, zero-lag in-game visual toast notification HUD.
      *(Tested thoroughly without issues found so far; please feel free to report if you encounter any problems.)*
45. **Fast Direct Member Access & Type-Safe VTable Dispatch (`DreamVTable`)**
    - Compile-time member pointer resolution and zero-overhead direct vtable slot invocation bypassing dynamic name lookups.
46. **Structured Configuration Engine (`DreamToml`)**
    - Lightweight, embedded zero-dependency TOML parser with typed section lookups and live runtime mutation.
47. **Live Runtime Memory Patch Verifier (`DreamPatchVerifier`)**
    - Pre-flight memory safety analyzer checking target instruction boundaries and active thread RIP positions before applying any hook.
48. **High-Performance Async Log Queue (`DreamLog v2`)**
    - Double-buffered zero-syscall non-blocking async logger with memory history buffer, drop-on-full protection, and background file flush thread.
49. **Headless Dedicated Server Guard (`DreamToast::IsHeadlessServer`)**
    - Automatic runtime environment detection that cleanly suppresses all Win32 GUI, DWM overlays, and desktop message loops on headless dedicated servers.
50. **Multi-DLL Proxy Chain-Loader (`DreamChainLoader`)**
    - Seamlessly chains multiple proxy DLLs (e.g. `dinput8`, `version`, `winmm`, `dxgi`), allowing ReShade and third-party tools to coexist without naming conflicts.
51. **Dynamic In-Memory Asset & LocaTag Bridge (`DreamAssetBridge`)**
    - Hot-patches game text, strings, and localized game tags (`LocaTag`) in memory without modifying pak/archive files on disk.
52. **Enshrouded 1.0+ ECS Archetype & Component Registry (`DreamEnshroudedTypes`)**
    - Complete archetype bitmask definitions, component IDs, and entity descriptors mapped to Enshrouded 1.0 engine layout.
53. **Extended 256-bit SIMD/AVX2 Register Context (`DreamMidHook` AVX2)**
    - Full preservation of 256-bit `YMM0`..`YMM15` registers alongside general-purpose registers (`RAX`..`R15`) in MidHook stubs with 32-byte alignment.
54. **Main-Thread Task Runner & Coroutines (`DreamScheduler`)**
    - Frame-synchronized main-thread task execution pipeline supporting `Enqueue`, delayed execution (`RunAfter`), recurring loops (`RunEvery`), and conditional coroutines (`YieldUntil`).
55. **Native In-Game ImGui Diagnostic Overlay (`DreamDiagnosticOverlay`)**
    - Real-time diagnostic HUD monitoring hook health, frame latency, memory allocation, and active mod status.
56. **Custom Network RPC Multiplexer (`DreamRpcMultiplexer`)**
    - Binary framed bi-directional RPC messaging between Client and Dedicated Server with automatic payload serialization and handler dispatch.
    - **Dual-Magic Protocol Framing:** Supports v2 28-byte header (`0xD7EA0002`) with explicit 32-bit sequence ID alongside backward-compatible legacy v1 24-byte framing (`0xD7EA0001`).
    - **Sliding-Window Replay Defense:** 64-bit sliding sequence bitmap for zero-allocation replay and duplicate packet filtering across socket hops.
    - **Token Bucket Rate Limiting:** 120 req/s refill rate with configurable burst token capacity and strict default DENY validator for remote callers.
57. **Embedded Atomic Key-Value Storage (`DreamStore`)**
    - Lightweight ACID-compliant atomic key-value persistence engine with atomic disk flush and typed getters/setters.
58. **Modern C++ Lua Binding Engine (`DreamLua` Extensions)**
    - Seamlessly exposes `dream.store`, `dream.overlay`, `dream.net`, and native C++ APIs to runtime Lua scripts.
59. **Game Update Binary Diff Auto-Resolver (`DreamPatchMatcher`)**
    - Profiles binary code signatures across game patches, resolving string cross-references and exporting transition reports automatically.
60. **Proprietary Voxel Engine API Abstraction (`DreamVoxelEngine`)**
    - Native abstraction layer hooking Keen Games voxel terraforming, structure snapping rules, and voxel volume damage lifecycle.
61. **Senior Tracing Harness (`DreamTrace`) & Vulkan Overlay (`DreamVkOverlay`)**
    - Nanosecond-precision execution phase recorder using a lock-free seqlock ring buffer with JSONL export and Vulkan frame sync.
62. **MSVC RTTI Keyword Scanner & VTable Slot Exporter (`DreamRttiScan`)**
    - Complete `.rdata` section RTTI memory scanner and cross-reference indexer querying candidate class slots and exporting TSV/JSON dumps.
63. **Senior Architecture Hardening (`DreamUnwind`, `DreamPatcher`, `DreamRelocator`)**
    - 8-byte minimal unwind layout (`MinimalUnwindInfo` with `UWOP_SET_FPREG`), lock-free atomic 5-byte CAS hot-patching (`_InterlockedCompareExchange64`), fast process thread freeze (`NtGetNextThread` ~18 µs), and 14-byte 64-bit absolute overlay hook forwarding.
64. **Keen Job Worker Fiber Guard & Vulkan Swapchain Lifecycle (`DreamFiberGuard`, `DreamVulkanTracker`)**
    - `ScopedFiberTebGuard` with stack range validation, Vulkan swapchain creation hook with `COLOR_ATTACHMENT` flag injection, monotonic `generation` ABA tracking, and segregated pool arenas (`InlineLeaf` vs `MidHookFramed`).
65. **Zero-Syscall Stack Bounds Cache & Race-Free Per-Stub Registration**
    - Direct-mapped lock-free 64-entry L1 cache (~2 ns latency) eliminating `VirtualQuery` in hot loops, and per-stub `RUNTIME_FUNCTION` registration eliminating table delete-readd race windows.
66. **3D World Cursor & Crosshair Targeting Reflection (`DreamEnshroudedTypes.hpp`, `DreamGameContext.hpp`)**
    - Deep reflection of Keen Engine 3D world cursor at `[R14 + 0x270]` with 52-byte packed struct (`DreamWorldCursor`). Exposes 3D world coordinates (`posX, posY, posZ`), normalized raycast orientation (`dirX, dirY, dirZ`), distance, and flags via C-ABI exports.
67. **Multi-Loader Coexistence Shield & Trampoline Forwarding (`DreamDisasm.hpp`, `DreamHook.hpp`)**
    - Detects pre-existing hooks (`0xE9`, `0xFF 0x25`, `0xEB`) installed by foreign loaders (EML, ShroudForge, Shroudtopia) or streaming overlays (OBS/Steam) and automatically chains execution via 14-byte 64-bit indirect jumps without clobbering foreign trampolines.
68. **Linux Proton / Wine Headless Dedicated Server Detection & Safe Threading (`DreamThread.hpp`)**
    - Dynamically detects Linux/Wine runtime environments via `ntdll!wine_get_version` and provides safe fallback for thread freeze operations in Docker/Pterodactyl dedicated servers.
69. **Keen Games KFC4 Container Binary Header Validation (`DreamAssetBridge.hpp`)**
    - 16-byte packed container header validation (`0x3443464B` magic / `"KFC4"`) and version extraction for Enshrouded 1.0 raw asset packages.

---

## Verification Suite (74/74 Tests Passed - 100% Green)

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
[TEST 42] Centralized Detour Dispatcher                -> [PASS] (Collision-proof inline detour sharing across mods!)
[TEST 43] Single-Header C++ Mod SDK (dream.hpp)        -> [PASS] (Single `#include <dream.hpp>` drop-in SDK!)
[TEST 44] Fuzzy Instruction-Aware AOB Scanner          -> [PASS] (HDE64 instruction-aware wildcarding across game updates!)
[TEST 45] Fast Direct Member Access & Type-Safe VTable -> [PASS] (Zero-overhead direct member & vtable dispatch!)
[TEST 46] Structured Binary Config Engine (DreamToml)  -> [PASS] (Lightweight TOML parser & live mutation!)
[TEST 47] Live Runtime Memory Patch Verifier           -> [PASS] (Pre-flight instruction boundary & thread safety verifier!)
[TEST 48] High-Performance Async Log Queue (DreamLog v2)-> [PASS] (Double-buffered zero-syscall non-blocking async logger!)
[TEST 49] Dedicated Server Headless Mode Guard         -> [PASS] (Zero GUI/DWM spawning on headless dedicated servers!)
[TEST 50] Multi-DLL Proxy Chain-Loader                 -> [PASS] (Proxy chaining for dinput8/version/winmm/dxgi coexistence!)
[TEST 51] Dynamic Engine Asset & LocaTag Bridge        -> [PASS] (In-memory localization tag & text overrides!)
[TEST 52] Enshrouded 1.0+ ECS Archetypes & Components  -> [PASS] (Component bitmasks & archetype table resolution!)
[TEST 53] Extended SIMD/AVX2 Register Context          -> [PASS] (Full YMM0-YMM15 256-bit SIMD register preservation!)
[TEST 54] DreamScheduler Task Runner & Coroutines      -> [PASS] (Main-thread task runner, delays, intervals, coroutines!)
[TEST 55] Native In-Game ImGui Diagnostic Overlay      -> [PASS] (Live hook tracker, FPS meter & memory diagnostics!)
[TEST 56] Custom Network RPC Multiplexer               -> [PASS] (Binary framed bi-directional RPC client <-> server!)
[TEST 57] Embedded Atomic Key-Value Storage (DreamStore)-> [PASS] (ACID-compliant atomic disk commit KV store!)
[TEST 58] Modern C++ Lua Binding Engine                -> [PASS] (Lua execution of dream.store, overlay & engine APIs!)
[TEST 59] Game Update Binary Diff Auto-Resolver        -> [PASS] (Automatic offset matching across game updates!)
[TEST 60] Proprietary Voxel Engine API Abstraction     -> [PASS] (Voxel terraforming, snapping rules & damage hooks!)
[TEST 61] Senior Tracing Harness & Present Hook        -> [PASS] (Nanosecond phase tracing, ring buffer & Vulkan overlay!)
[TEST 62] MSVC RTTI Keyword Scanner & VTable Exporter  -> [PASS] (Full binary RTTI walking & candidate query TSV/JSON export!)
[TEST 63] Senior Dev Architecture Hardening            -> [PASS] (MinimalUnwindInfo, 8-byte atomic CAS, NtGetNextThread freeze ~18 µs, 14-byte overlay chaining!)
[TEST 64] Keen Job Worker Fibers & Vulkan Swapchain    -> [PASS] (ScopedFiberTebGuard stack validation, Vulkan ABA generation tracking, Leaf vs Framed pool segregation!)
[TEST 65] Zero-Syscall Stack Bounds Cache & Unwind Reg -> [PASS] (64-entry direct-mapped lock-free cache ~2 ns, independent per-stub RUNTIME_FUNCTION!)
[TEST 66] 3D World Cursor & Crosshair Targeting Reflect -> [PASS] (Dissected [R14 + 0x270], 52-byte struct, normalized raycast & C-ABI exports!)
[TEST 67] Multi-Loader Coexistence Shield & Forwarding  -> [PASS] (Detected foreign E9/FF 25/EB jumps, 14-byte trampoline forwarding!)
[TEST 68] Linux Proton / Wine Headless Server Detection -> [PASS] (ntdll!wine_get_version probing & safe fallback for thread freeze!)
[TEST 69] Keen Games KFC4 Container Binary Validation   -> [PASS] (16-byte packed header, 'KFC4' 0x3443464B magic check & version extraction!)
[TEST 70] MidHook RIP Displacement & IpMap Relocation -> [PASS] (Intra-instruction displacement mapping & widened 16-bit IpMap!)
[TEST 71] RtlVirtualUnwind Stack Walk & MinimalUnwindInfo-> [PASS] (Verified 44-byte reverse prologue unwind info with Windows SEH!)
[TEST 72] VEH CPU Register Context & Hardware Alignment  -> [PASS] (16-byte XMM stride preservation & strict hardware breakpoint alignment!)
[TEST 73] DreamStore Hex Blobs, Strings & Dirty Versions -> [PASS] (Hex-encoded binary blobs, double %.17g precision & versioned flushes!)
[TEST 74] DreamRpcMultiplexer Anti-Spoofing & Rate Limit -> [PASS] (Strict caller validation, default DENY, 120/s rate limit & duplicate check!)
```

**ALL 74 TESTS PASSED! dreamkurumiHT IS FULLY VERIFIED! (100% Green)**

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
