# dreamkurumiHT Changelog

All notable changes to the **dreamkurumiHT** Native Harmony Modding Framework will be documented in this file.

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
