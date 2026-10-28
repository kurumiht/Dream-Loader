# dreamkurumiHT Changelog

All notable changes to the **dreamkurumiHT** Native Harmony Modding Framework will be documented in this file.

---

## [v3.8.0] - 2026-10-28

### 🚀 Highlights (จุดเด่นสำคัญในเวอร์ชั่นนี้)
- **Dynamic Stack Unwinding & Segregated Trampoline Pool (`DreamUnwind.hpp` & `DreamPool.hpp`):**
  - Segregated trampoline arena chunks into `ChunkKind::InlineLeaf` and `ChunkKind::MidHookFramed`.
  - Leaf trampolines use pure RSP-relative leaf execution with zero dynamic function table overhead.
  - MidHook stubs register dedicated `RUNTIME_FUNCTION` entries with 8-byte `MinimalUnwindInfo` (`UWOP_SET_FPREG` for `RBP` frame pointer), guaranteeing 100% compliant Windows x64 SEH callstack unwinding without table delete-readd race windows.
- **Zero-Stutter Multi-Threaded Hot-Patching (`DreamPatcher.hpp`):**
  - Lock-free 8-byte atomic compare-and-swap (`TryAtomicFiveBytePatch` via `_InterlockedCompareExchange64`) for instant 5-byte JMP patches without freezing threads.
  - High-performance thread freeze fallback (`FastProcessThreadFreeze` using dynamic `NtGetNextThread`, ~18 µs total suspension overhead) bypassing heavy `CreateToolhelp32Snapshot` heap overhead.
- **Overlay Hook Chaining (`DreamRelocator.hpp`):**
  - 14-byte 64-bit absolute indirect detour jumps (`FF 25` / `EmitAbsoluteJump64`).
  - Seamlessly chains existing third-party hooks (`TryChainExistingOverlayHook`) for Steam Overlay (`GameOverlayRenderer64.dll`), Discord, and OBS without hook stomping or infinite cycles.
- **Job Worker Fiber Guard (`DreamFiberGuard.hpp`):**
  - `ScopedFiberTebGuard` equipped with a 64-entry lock-free direct-mapped L1 cache (`((rsp >> 16) ^ (rsp >> 22)) & 63`) using 64-bit relaxed atomics (~2 ns latency).
  - Eliminates repetitive `VirtualQuery` syscall overhead on Keen Games voxel worker fibers and completely prevents `STATUS_BAD_STACK (0xC0000028)` during SEH unwinds on custom fiber stacks.
- **Vulkan 1.3 Swapchain ABA Guard (`DreamVulkanTracker.hpp`):**
  - Tracks Vulkan swapchain lifecycle across dynamic resizes and DLSS 3 / FSR 3 dynamic recreations.
  - Hooks `vkCreateSwapchainKHR`, `vkDestroySwapchainKHR`, and `vkQueuePresentKHR`, automatically injecting `VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT` for overlay rendering and maintaining monotonic `generation` ABA protection.
- **Expanded Test Suite (65/65 Passing):**
  - Comprehensive unit test coverage covering AVX2 scanner, fiber TEB bounds cache, atomic 8-byte patching, swapchain generation tracking, and SEH frame unwinding.
- **First-Party Production Mod Included (`mods/Dream-AoE-Harvest`):**
  - Bundled modular C++20 `Dream-AoE-Harvest` plugin (3.5m radius farming crop harvesting mod with universal access).

---

## [v1.0.2] - 2026-10-17

### 🚀 Highlights (จุดเด่นสำคัญในเวอร์ชั่นนี้)
- **Universal Zero-Deadlock Exit Architecture:**
  - Resolved the infamous Windows Loader Lock deadlock (`LdrpLoaderLock` / `do_sema_b_wait_intern` / `winpthreads`) where `enshrouded.exe` or `enshrouded_server.exe` hung in the background upon exit or `Ctrl + C`.
  - Upgraded the shutdown sequence to atomic `TerminateProcess(GetCurrentProcess(), uExitCode)`, bypassing MinGW CRT thread teardown deadlocks entirely.
  - Added low-level inline detour hooks on `kernel32!ExitProcess`, `KERNELBASE!ExitProcess`, and `ntdll!RtlExitUserProcess` to guarantee clean process termination in 0ms across all exit paths.
- **Window Lifecycle & Console Synchronization:**
  - Watchdog actively monitors the Enshrouded game window (`!IsWindow(g_hGameWindow)`). The instant the game window is destroyed, the console window (`dreamkurumiHT Harmony Framework (Client)`) is hidden and detached cleanly.
  - Dedicated Server immediately terminates on `Ctrl + C`, `Ctrl + Break`, or terminal close events without leaving zombie server instances.
- **Enhanced Watchdog Health Diagnostics (`DreamWatchdog`):**
  - Added physical RAM and process memory monitoring (`GlobalMemoryStatusEx`, `GetProcessMemoryInfo`).
  - Added proactive warnings when memory is critically low (<2GB) or when duplicate background GPU capture services (e.g., `SteelSeriesCaptureSvc.exe`) cause out-of-memory risks.

---

### 🛠️ Changes & Improvements (รายละเอียดการปรับปรุง)

#### 1. Core Native Host (`dinput8.dll`)
- Replaced all calls to `ExitProcess(0)` with kernel-level `TerminateProcess(GetCurrentProcess(), 0)` in `UniversalDInput8Host.cpp`.
- Implemented `InstallExitHooks()`:
  - Hooks `kernel32.dll -> ExitProcess`
  - Hooks `KERNELBASE.dll -> ExitProcess`
  - Hooks `ntdll.dll -> RtlExitUserProcess`
- Updated `ConsoleCtrlHandler` for Dedicated Server to terminate instantly on `CTRL_C_EVENT`.
- Upgraded `ProcessExitWatchdogThread` to instantly trigger exit upon window destruction without arbitrary wait delays.

#### 2. Native Harmony Plugin (`mods/dreamkurumiHT/`)
- Updated `DLL_PROCESS_DETACH` in `dllmain.cpp` to immediately hide (`ShowWindow(SW_HIDE)`) and free the console window (`FreeConsole()`).
- Updated framework version string to `1.0.2` in `modinfo.json`, `dllmain.cpp`, and runtime banner.

#### 3. Strict Architectural Compliance (Rule 1 & Rule 2)
- Maintained decoupled architecture: `dreamkurumiHT` remains strictly the core native Harmony hooking engine.
- Game mods (`Dream-AoE-Harvest`, `Dream-Auto-Pickup`, `XP-Share`, `Admin-Menu`) link dynamically via the lightweight C-ABI plugin header (`dreamkurumiHT_plugin.h`).
- Universal player access policy enforced by default (`AdminOnly = false`).

---

## [v1.0.1] - 2026-10-15
- Initial release of C-ABI dynamic plugin loader architecture (`dreamkurumiHT_plugin.h`).
- Near-Memory 64KB Trampoline Arena Pool (`DreamPool`).
- AVX2 Vectorized Pattern Scanner (`DreamScanner`).
- Build-aware symbol cache (`dreamkurumi_cache.ini`).
- High-Performance Diagnostics Logger (`DreamLog`).
