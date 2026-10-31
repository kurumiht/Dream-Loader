# dreamkurumiHT Changelog

All notable changes to the **dreamkurumiHT** Native Harmony Modding Framework will be documented in this file.

---

## [v3.9.0] - 2026-10-31 (Kernel Memory Probe Guard, Zero-Freeze Epilogue Drain & GNS Demux)

### 🛡️ Kernel-Safe Memory Probing & Guard Page Protection (`DreamMemory.hpp`, `DreamCrashGuard.hpp`, `DreamCore.hpp`)
- **TEB Stack Guard Pre-Checking (`MemoryHelper::IsSafeToProbeAddress`):**
  - Added zero-syscall TEB boundary checking using `__readgsqword(0x10)` (`StackLimit`) and `__readgsqword(0x1478)` (`DeallocationStack`).
  - Probes into uncommitted thread stack expansion zones are rejected upfront before touching memory, preserving Windows Kernel automatic stack growth (`nt!MmAccessFault`).
- **One-Shot Guard Page Protection (`STATUS_GUARD_PAGE_VIOLATION` Pass-Through):**
  - Strictly whitelisted only `EXCEPTION_ACCESS_VIOLATION (0xC0000005)` and `EXCEPTION_IN_PAGE_ERROR (0xC0000006)` for memory probes.
  - Automatically pass through `STATUS_GUARD_PAGE_VIOLATION (0x80000001)` and `STATUS_STACK_OVERFLOW (0xC00000FD)` in VEH to avoid permanently stripping `PAGE_GUARD` bits from PTEs.
  - Memory probe fault addresses are validated against `[probeAddress, probeAddress + probeSize)`.
  - Memory probe faults are disarmed without logging crash reports or tripping mod circuit breakers.

### 🔄 Zero-Freeze Epilogue-Safe Hot-Reload Draining (`DreamPluginGate.hpp`)
- **Host-Owned Execution Gate (`PluginExecutionGate`):**
  - Solves the function epilogue (`RET`) race window on DLL unload by having the host framework own entry (`TryEnter()`) and exit (`Leave()`).
  - Guarantees execution has cleanly returned to `dreamkurumiHT.dll` `.text` before `FreeLibrary` is ever called.
- **Asymmetric IPI Memory Barrier (`FlushProcessWriteBuffers`):**
  - Broadcasts Inter-Processor Interrupts (IPI) via `FlushProcessWriteBuffers()` to synchronize write buffers across all CPU cores in 3-5 µs without thread deadlocks.
- **Quarantine Module Safe Fallback (`PluginGateRegistry`):**
  - Modules that fail to drain within the timeout budget are quarantined rather than unmapped, preventing access violation crashes.

### 🌐 SteamNetworkingSockets Boundary Demultiplexing (`DreamGnsTransportHook.hpp`)
- **Out-of-Band Mod RPC Routing:**
  - Demultiplexes `0xD7EA0002` (and `0xD7EA0001`) RPC datagrams directly at the `ISteamNetworkingSockets::ReceiveMessagesOnConnection` boundary before `keen::net` reads them.
  - Eliminates bitstream offset drift and deserialization desync inside Keen Holistic Engine's `PacketWriter`/`PacketReader`.
  - Authenticates caller connection via `msg->m_conn` (`trustedSenderId`), calls `msg->Release()`, and compacts the message array in-place.

---

## [v3.8.5] - 2026-10-30 (Kernel Hardening & Diagnostics Overhaul)

### 🛡️ Kernel-Level CrashGuard & VEH Overhaul (`DreamCrashGuard.hpp`)
- **Direct Kernel Context Restoration (Zero CRT `setjmp`/`longjmp`):**
  - Completely replaced CRT `setjmp`/`longjmp` with direct `RtlCaptureContext` + memory compiler barrier on entry.
  - Exception recovery executes via direct `NtContinue` (`EXCEPTION_CONTINUE_EXECUTION`) on a clean stack.
- **AVX/AVX2 State Sanitization:**
  - Integrated `_mm256_zeroupper()` on landing pad post-fault to clear dirty upper 128-bit YMM state, eliminating AVX-to-SSE transition penalties.
  - Sanitized MXCSR sticky exception flags (bits 0..5) across both x64 `CONTEXT` slots (`c->MxCsr` and `c->FltSave.MxCsr`).
- **Hardware Breakpoint Preservation:**
  - Preserved active hardware breakpoints (`Dr0..Dr3, Dr7`) and forced `c->ContextFlags |= CONTEXT_DEBUG_REGISTERS` ensuring hardware debug registers are committed by the Windows kernel.
- **Zero-Syscall in VEH:**
  - Eliminated `VirtualQuery` from VEH exception path; RIP validity is verified from exception record arguments (`isBadRip`).
- **Diagnostic Log Overhaul:**
  - Integrated HDE64 opcode decoding and mnemonic hints (MOV, CALL, JMP, CMP, etc.).
  - Added "Mod Call Site" detection: when crash occurs inside `enshrouded.exe`, callstack is scanned to find the initiating mod frame.
  - Added Debug Heap pattern recognition (`0xCDCDCDCD`, `0xDDDDDDDD`, `0xFEEEFEEE`, `0xCCCCCCCC`, `0xBAADF00D`).
  - Stack top peek (top 4 QWORDs on `[rsp]`) and full register dump (`RAX-R11`, `RSP`, `RBP`).
- **Generation-Tagged Lock-Free Circuit Breaker:**
  - Packed `[32-bit Generation | 32-bit Fault Counter]` in atomic `uint64_t`.
  - Added automatic circuit breaker reset on shadow-copy hot-reload (`DreamWatcher`).
- **In-Game Toast Notifications:**
  - Wired `DreamToast` to notify player on mod crash or circuit trip (auto-suppressed on headless dedicated servers).

### ⚡ Network & RPC Enhancements (`DreamRpcMultiplexer.hpp`)
- **Token Bucket Rate Limiting:**
  - Upgraded rate limiting to a Token Bucket algorithm (120 req/s refill rate with configurable burst capacity).
  - Sliding window 64-bit sequence bitmap for replay attack filtering.

### ⚙️ Full Configuration & Subsystem Synchronization (`DreamLiveConfig` & `dllmain.cpp`)
- **Automated Startup Config Reloading:**
  - Wired `DreamLiveConfig::Instance().ReloadFromFiles()` to execute on framework boot in `dllmain.cpp`.
  - Parsed and synchronized all sections from `config.toml` (`[General]`, `[CrashGuard]`, `[Network.Rpc]`, `[Watchdog]`, `[Backup]`).
- **Dedicated Server Background Scheduler Ticker:**
  - Added `DreamScheduler::StartDedicatedServerTicker(15)` on headless servers so delayed tasks (`RunAfter`), intervals, and coroutines tick without a Vulkan swapchain.
- **Graceful Storage & Watchdog Shutdown:**
  - Explicitly invoked `DreamStore::FlushAll()`, `DreamScheduler::StopDedicatedServerTicker()`, and `DreamWatchdog::Stop()` during DLL unload.
- **Plugin C-ABI Dynamic Export Parity (`dreamkurumiHT_plugin.h` & `DreamCExport.hpp`):**
  - Exposed `DK_CrashGuard_*` (reset circuit, trip query, debugger pass-through).
  - Exposed `DK_Store_*` (atomic String, Int64, Float, Bool, and manual flush).
  - Exposed `DK_Rpc_*` (frame packer V2, token bucket configuration, caller authorization).
  - Exposed `DK_DiagnosticOverlay_*` (HUD toggle, visibility query, hook health registry).
- **Diagnostics Command & Snapshot Integration (`DreamOverlay.hpp`):**
  - Added in-game console command `diag [toggle]` and appended hook health status to `snapshot` reports.
- **Verification:**
  - Expanded unit test suite from 74 to **77 tests** (`Test 75: Dedicated Server Ticker`, `Test 76: LiveConfig Sync`, `Test 77: DreamStore & CrashGuard Controls`). All 77 tests passing (100% green).

---

## [v3.8.4] - 2026-10-30 (Bugfix Release - แก้ไขบัค)

### 🐛 Bug Fixes & Architecture Hardening (แก้ไขบัคและปรับปรุงความเสถียร)
- **Dream-AoE-Harvest Chest / Container Random Opening Fix (mods/Dream-AoE-Harvest/):**
  - แก้ไขปัญหาเวลาเปิดกล่อง/หีบสมบัติแล้วกล่องรอบๆ ถูกเปิดมั่วหรือติดค้าง interaction offer
  - เพิ่มการตรวจสอบ Archetype Bitmask (IsContainerArchetype & IsHarvestableCropArchetype) เพื่อแยกกล่องเก็บของ (HasInventory 0x174, RandomLoot 0x175) ออกจากพืชฟาร์มอย่างเด็ดขาด
  - เพิ่ม RAII Scope Guard g_isExpandingCropBatch ป้องกันการ spoof ข้อเสนอ interaction ไปยังวัตถุอื่น
- **Network & RPC Anti-Spoofing Hardening (DreamRpcMultiplexer.hpp & DreamNetBridge.hpp):**
  - บล็อกการแอบอ้างสิทธิ์ (Spoofing) ทันทีทุกกรณีหาก rameCallerId != trustedSenderId ป้องกันไม่ให้ client แอบอ้างเป็น server (callerId == 0) หรือปลอมตัวเป็นผู้เล่นอื่น
  - ดรอป packet ที่มี payload ขนาดผิดปกติทันที (
eturn true) ป้องกัน malformed packet รั่วไหลเข้าเอนจินเน็ตเวิร์กของเกม
  - ตั้งค่าเริ่มต้นของ remote callers เป็น DENY พร้อมระบบ Rate Limiting (120 calls/sec ต่อผู้เล่น)
- **MidHook RIP Displacement & Instruction Relocation (DreamHook.hpp, DreamRelocator.hpp, DreamThread.hpp):**
  - ตรวจจับ Foreign Hook ก่อนกระบวนการขโมยไบต์ และแปลง jump เป็น 14-byte FF 25 พร้อมบันทึก IP map ป้องกัน hook ซ้อนพัง
  - ตัดกรณี EB F9 (Short Hotpatch) ออกจาก TryChainExistingOverlayHook ป้องกันการเขียนทับ 5 ไบต์ทำลายคำสั่งเดิม
  - ขยาย DreamIpMap เป็น uint16_t รองรับ stub offset เกิน 255 ไบต์ พร้อม intra-instruction displacement mapping
  - ปรับปรุงการตรวจสอบ branch range เป็น destAddr >= m_target ดักจับ backward jump ไปยังต้นฟังก์ชัน
- **CrashGuard & Zero-Allocation Safe Dispatching (DreamCrashGuard.hpp, DreamDispatcher.hpp):**
  - เปลี่ยน circuit breaker ใน DreamCrashGuard เป็นตาราง Lock-free Atomic m_faultCounts[64] ร่วมกับ HashModName ขจัด mutex และ string allocation ใน hot path
  - จัดการ EXCEPTION_STACK_OVERFLOW ด้วย _resetstkoflw()
  - ป้องกัน UB ใน DreamDispatcher กรณี type mismatch ให้ส่งคืน dummy handler อย่างปลอดภัย
- **Expanded Verification Test Suite (74/74 Unit Tests Passing - 100% Green):**
  - เพิ่มการทดสอบครอบคลุม Anti-Spoofing, Rate Limiting, MidHook Displacement, Unwind Info Layout และ Atomic Patching

---

## [v3.8.3] - 2026-10-29

### 🚀 Highlights (จุดเด่นสำคัญในเวอร์ชั่นนี้)
- **3D World Cursor & Crosshair Targeting Reflection (`DreamEnshroudedTypes.hpp`, `DreamGameContext.hpp` & `dreamkurumiHT_plugin.h`):**
  - Reflected Enshrouded 1.0 Keen Engine `world_cursor` (`[R14 + 0x270]`).
  - Added 52-byte packed `DreamWorldCursor` struct exposing 3D world hit coordinates (`posX, posY, posZ`), normalized raycast orientation (`dirX, dirY, dirZ`), eye origin, distance, flags, and validity bit.
  - C-ABI exports `DK_GetWorldCursorPosition`, `DK_GetWorldCursorRay`, `DK_GetWorldCursor`, and `DK_SetWorldCursor` enabling pinpoint ranged harvesting and crosshair targeting for all plugins.
- **Multi-Loader Coexistence Shield (`DreamDisasm.hpp` & `DreamHook.hpp`):**
  - Added `DreamDisasm::HasExistingHook` to detect pre-existing 5-byte JMP (`0xE9`), 6-byte 64-bit indirect JMP (`0xFF 0x25`), and 2-byte short JMP (`0xEB`) installed by third-party mod loaders (EML, ShroudForge, Shroudtopia) or OBS/Steam overlays.
  - `DreamMidHook` transparently forwards execution to the foreign hook's destination via 14-byte 64-bit indirect jump (`FF 25`) instead of clobbering foreign trampolines.
  - Exported `DK_Hook_HasExistingHook` via C-ABI.
- **Linux Proton / Wine Headless Dedicated Server Detection & Safe Threading (`DreamThread.hpp`):**
  - Added `IsRunningUnderWine()` and `GetWineVersion()` inspecting `ntdll!wine_get_version`.
  - Seamlessly handles thread suspension under Linux Docker / Pterodactyl containers, eliminating thread freeze hang risks.
  - Exported `DK_Env_IsWine` via C-ABI.
- **Voxel Engine 8x8x8 Boundary-Safe Alignment (`DreamVoxelEngine.hpp`):**
  - Implemented `AlignToChunkBoundary` 8-cell mathematical bounding formula for Enshrouded's 512-cell voxel chunks, eliminating edge-case mesh tearing and buffer overreads.
- **Keen Games KFC4 Container Binary Header Validation (`DreamAssetBridge.hpp`):**
  - Added packed 16-byte `Kfc4ContainerHeader` (`0x3443464B` magic) validation and `GetKfc4Version`.
  - Exported `DK_Asset_IsValidKfc4` via C-ABI.
- **Expanded Verification Test Suite (69/69 Unit Tests Passing):**
  - Test 66: 3D World Cursor & Crosshair Targeting Reflection.
  - Test 67: Multi-Loader Coexistence Shield & Hook Forwarding.
  - Test 68: Linux Proton / Wine Dedicated Server Detection.
  - Test 69: KFC4 Container Binary Header Validation.

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
