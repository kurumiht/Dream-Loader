# dreamkurumiHT Changelog

All notable changes to the **dreamkurumiHT** Native Harmony Modding Framework will be documented in this file.

---

## [v3.9.2] - 2026-11-05 (Official Release - Enshrouded 1.0.1.1 Adaptation, Spatial Query Engine & Core Hardening)

### Summary
Official production release delivering comprehensive support for Enshrouded v1.0.1.1, spatial ECS snapshot queries, live console stability, and deep subsystem fault-tolerance hardening.

### Highlights
- **Enshrouded 1.0.1.1 Engine Adaptation:** Direct O(1) loop recovery and automated PE timestamp invalidation (`0x6AEB143E` / `0x6AEB144C`), seamlessly updating cached RVAs without player intervention.
- **Native Spatial Query Engine (`DreamWorldQuery`):** Zero-allocation spatial snapshot queries with archetype mask extraction for high-throughput world entity tracking.
- **Interactive Console & QuickEdit Hardening (`DreamOverlay`):** Stripped Windows Console QuickEdit mode to eliminate mouse-click game freeze; preserved background keystroke buffer against input dropping.
- **TOML & Configuration Robustness (`DreamToml`):** Quote-aware string array parser maintaining comma integrity inside quoted values.
- **SIMD Scanner & Memory Page Protection (`DreamScanner`):** Strict page boundary guards on AVX2/SSE2 vector scans preventing read violations near `PAGE_NOACCESS` boundaries.
- **Disassembler Wildcard Accuracy (`DreamFuzzyScanner`):** Rel8 short jump displacement underflow protection for reliable AOB generation.
- **Adaptive Throttler Guard (`DreamDynamicBackpressure`):** Hardened QPC frequency divisor against zero-division faults.
- **100% C-ABI v1 Parity:** Fully verified 140/140 exported APIs and 103/103 automated verification tests.

---

## [v3.9.1] - 2026-11-01 (Official Release - Session Epoch Auto-Resync, High-Jitter Resilience & Mutual Exclusivity)

### Summary
Official production release promoting intermediate revisions v3.9.0 v1.0 through v3.9.0 v1.11 to stable v3.9.1.

### Highlights
- Server Session Epoch & Zero-Touch Auto-Resync (reconnect & server restart safe recovery)
- VPN & High-Jitter Network Resilience (configurable 10s watchdog heartbeat tolerance)
- Clean Default Keybindings (Diagnostic HUD overlay disabled by default)
- Crop Harvest & Ground Loot Mutual Exclusivity
- C-ABI v1 100% Binary Backward Compatibility

---

## [v3.9.0 v1.11] - 2026-11-01 (Configurable SessionSync, F9 Disabled by Default & Auto-Pickup Configs)



### Core Engine Configuration Hardening

- **F9 Diagnostic HUD Disabled by Default:**

  - Configured `EnableDiagnosticsHUD = false` by default across `config.toml`, `config.ini`, `DreamDiagnosticOverlay.hpp`, and `DreamLiveConfig.hpp`.

  - Ensures the F9 key remains completely free for other user keybindings unless explicitly enabled.

- **Dedicated [SessionSync] Configuration Section:**

  - Exposed `EnableSessionSync = true`, `WatchdogTimeoutSeconds = 10`, and `BroadcastIntervalMs = 1000` in both `config.toml` and `config.ini`.

  - `DreamSessionSync` now dynamically loads and respects custom watchdog timeout and heartbeat intervals.



### Plugin Configuration Polish

- **Dream-Auto-Pickup Settings:**

  - Documented and exposed `SameTypeOnly = false` and `ShiftKeyVanillaMode = true` in both `config.toml` and `config.ini`.



---



## [v3.9.0 v1.10] - 2026-11-01 (Multi-Plugin Mutual Exclusivity, Auto-Resync Hardening & Buffer Clamping)

### 🌾 Crop Harvest & Ground Loot Mutual Exclusivity (Dream-AoE-Harvest & Dream-Auto-Pickup)
- **Strict Ground Loot vs Farm Crop Classification:**
  - Added IsGroundLootArchetype detecting Word 5 components 326..334 (PickupItem, DestroyOnLoot, AreaLootOverride, etc.).
  - Dream-AoE-Harvest explicitly aborts if IsGroundLootArchetype(pPrimaryArch) is true, guaranteeing it never touches dropped wood, stones, ores, or monster drops.
  - Dream-Auto-Pickup explicitly aborts if !IsGroundLootArchetype(pPrimaryArch) is true, guaranteeing it never intercepts farm crops or non-loot world objects.
  - Eliminates batch clobbering and buffer index contention between the two chained plugins.
- **Buffer Safety Invariant (Dream-Auto-Pickup):**
  - Added strict slot clamping: maxSlotsRemaining = (currentCount < 15) ? (15 - currentCount) : 0.
  - Prevents writing byte markers beyond the 2048-byte single chunk boundary.

### 🌐 Session Epoch & Auto-Resync Machine Hardening (DreamSessionSync.hpp)
- **Stale Context Cache Invalidation:**
  - On incoming epoch change, immediately invalidates DreamGameContext::LocalPlayer and World pointers (
ullptr).
  - YieldUntil now strictly waits for fresh local player spawn rather than clearing safe-mode prematurely on a timer with stale session pointers.
- **VPN / High-Jitter Network Tolerance:**
  - Increased dedicated server epoch broadcast frequency to **1000ms** (RunEvery(1000, ...)).
  - Extended client heartbeat watchdog timeout from **6000ms** to **10000ms** (10 seconds), preventing false-positive safe-mode drops across Radmin VPN or Wi-Fi jitter.
- **Anti-Spoofing Guard:**
  - In InitializeClientListener, added verification if (!pkt.IsFromServer() && !DreamNetBridge::Instance().IsLoopbackEnabled()) return; to prevent foreign client epoch spoofing.

---

## [v3.9.0 v1.6] - 2026-11-01 (F9 Diagnostics, Window Minimization, Live Network Tracking & AoE Harvest Fixes)

### 🖥️ Native F9 HUD & Window Management
- **Crash-Proof Window Synchronization (`DreamWindowSync.hpp`):**
  - Completely replaced fragile cross-thread `GWLP_WNDPROC` subclassing of the game window with native Win32 window ownership (`SetWindowLongPtrW(m_hWnd, GWLP_HWNDPARENT, hGameWnd)`).
  - Eliminates the crash that occurred when opening the F9 menu after joining a dedicated server (where swapchain recreation collided with foreign message procedure hooks).
  - Safe for all Vulkan, SDL, and GLFW window management workflows.
- **Window Minimization & Alt-Tab Focus Fix (`DreamDiagnosticOverlay.hpp`):**
  - Removed `WS_EX_TOPMOST` and desktop-wide floating behavior.
  - As an owned window (`GWLP_HWNDPARENT`), Windows OS automatically hides the F9 overlay when the game is minimized and keeps it behind other active desktop applications (Discord, Chrome, etc.).
  - Added strict foreground window filtering (`GetForegroundWindow() != hTargetWnd`) and `m_redrawMutex` synchronization to guarantee GDI double-buffering is 100% thread-safe.
  - Added NULL-pointer guards for `CreateDIBSection` allocations to eliminate secondary Access Violations.

### 🌐 Live Steam / GNS Network State Tracking
- **Automatic SteamNetworkingSockets Interceptor (`DreamGnsTransportHook.hpp`):**
  - Added dynamic runtime hooking for `steam_api64.dll` exports: `SteamAPI_ISteamNetworkingSockets_ReceiveMessagesOnConnection`, `SteamAPI_ISteamNetworkingSockets_GetConnectionRealTimeStatus`, and `SteamAPI_ISteamNetworkingSockets_CloseConnection`.
  - Automatically queries live connection quality and round-trip ping (ms) without touching internal engine offsets.
  - Solved the issue where F9 HUD showed `Server Connection: Disconnected / Local` even when connected to a dedicated multiplayer server.
  - Added fallback in `DreamNetStateTracker` (`IsClientConnectedToRemoteServer()`) to detect active multiplayer sessions seamlessly.

### 📦 Native Plugins Ecosystem Updates
- **Dream-AoE-Harvest Fixes (`mods\Dream-AoE-Harvest`):**
  - Fixed `Hook_GetInteractionOffer` by removing premature local scoping flag that disabled offer synthesis before the engine's outer batch loop executed.
  - Corrected `IsContainerArchetype` mask: removed erroneous `CONTAINER_WORD4_MASK` and restricted container checks strictly to Word 5 (Components 372, 373, 382).
  - Primary crop archetype is no longer dropped if it lacks bit 43: whatever crop the player interacted with is accepted and matched against identical nearby crops.
- **Dual-Plugin Chaining Protocol (`Dream-AoE-Harvest` & `Dream-Auto-Pickup`):**
  - Implemented transparent multi-hook chaining in mod assembly stubs (`AoeHarvestAsm.s` and `AutoPickupAsm.s`).
  - When one mod hooks onto another pre-existing mod detour, it skips executing displaced instructions in the intermediate hook and passes control down the chain cleanly.
  - Displaced instructions are executed exactly once at the terminal return to game engine code, completely eliminating duplicate stack writes, clobbered `rax`/`r8d`, and register corruption.

### 🧪 Verification & Build
- **Tests:** 96/96 unit tests passing 100% Green (`build_test.bat`).
- **ABI Golden Check:** 131/131 API exports verified, 0 unlisted exports (`check_exports.py`).
- **ABI Smoke Test:** Binary golden plugin compatibility verified (`golden_host_test.exe`).
- **Master Build:** All 5 modules recompiled and auto-synchronized cleanly via `build_all.bat`.

---

## [v3.9.0 v1.5] - 2026-11-01 (Full Codebase Audit & Hardening)

### 🛡️ Core Engine Hardening & Bug Fixes
- **ConfigEntry Server-Lock Sync Guard (`DreamLiveConfig.hpp`):**
  - Fixed an issue where `ConfigEntry<T>::Set` prematurely updated its cached value pointer before verifying server lock status.
  - Now verifies `DreamLiveConfig::Set` return value: if enforced by Dedicated Server, the local mutation is rejected and resynchronized to the server-locked value.
- **NetBufferReader Error Latching (`DreamNetBridge.hpp`):**
  - Added sticky error state (`HasError()`, `ClearError()`, `m_error`) to `NetBufferReader`.
  - Out-of-bounds reads and truncated strings immediately latch `m_error = true`, preventing deserialization desynchronization.
- **CrashGuard Heuristic & VEH Page-Boundary Safety (`DreamCrashGuard.hpp`):**
  - Corrected x64 REX prefix offset parsing in `IsLikelyReturnAddress` for 3-byte (`41 FF D0..D7`), 4-byte (`41 FF 50..57`), and 7-byte (`41 FF 90..97`) indirect calls.
  - Clamped opcode sniffing in `VectoredCrashHandler` to the 4KB page boundary (`0x1000 - (RIP & 0xFFF)`), eliminating secondary Access Violations.
  - Added TLS cleanup on C++ exception propagation in `Execute`, preventing dangling stack pointers in the thread's TLS slot.
- **Disassembly Underflow & Relocation Bounds Guards (`DreamDisasm.hpp`, `DreamHook.hpp`):**
  - Guarded `dispOffset` and `immOffset` calculations against `uint8_t` underflow on malformed opcodes.
  - Added strict destination buffer bounds checks before writing relocated RIP-relative displacements in `DreamInlineHook` and `DreamMidHook`.
- **Deterministic Hook Subscriber Ordering (`DreamDispatcher.hpp`, `DreamPipeline.hpp`):**
  - Replaced `std::sort` with `std::stable_sort` across central detour and hook pipeline subscriber registries to guarantee deterministic FIFO execution when priorities match.
- **TOML Parser Standards Compliance (`DreamToml.hpp`):**
  - Fixed quote boundary detection when lines contain escaped backslashes (e.g. Windows paths like `H:\\Games\\Enshrouded\\`).
  - Added standard TOML basic string unescaping for double-quoted strings (`\"`, `\\`, `\n`, `\t`, `\r`).
- **Shadow VMT Memory Boundary Protection (`DreamVTable.hpp`):**
  - Added `VirtualQuery` readable page clamping to `DreamShadowVTable::Initialize`, preventing out-of-bounds reads during per-instance vtable cloning.

### 🧪 Test Suite & ABI Verification
- **Test Suite Expansion:**
  - Added **Test 96** comprehensively verifying `NetBufferReader` error latching, `ConfigEntry` server-lock anti-tamper, `DreamToml` path escapes & unescaping, `DreamDisasm` underflow resilience, deterministic dispatcher FIFO ordering, and `DreamCrashGuard` exception TLS safety.
  - **All 96 tests passing 100% Green**.
- **ABI Golden Verification:**
  - Passed `check_exports.py` with 131 official API symbols and 0 unlisted exports.
  - Passed `golden_host_test.exe` binary compatibility test (`smoke=0`).
- **Master Build:**
  - Built and auto-synchronized all 5 modules to `EnshroudedServer`: `dreamkurumiHT.dll`, `DreamAoEHarvest.dll`, `DreamAutoPickup.dll`, `XPShare.dll`, `AdminMenu.dll`.

---

## [v3.9.0 v1.4] - 2026-10-31 (ServerSync Handshake Gate & Dedicated Server Config Locking)

### 🌐 Networking & Server-Enforced Synchronization (ServerSync)
- **Dedicated Server Mod Version Handshake Gate (`DreamNetBridge.hpp`):**
  - Implemented `DreamServerSync::EvaluateHandshake` on Dedicated Server to validate client Core versions and required mod versions (`RequireMod(name, minVersion, maxVersion)`).
  - Automatically evaluates semantic versions (`CompareVersions`) across major, minor, and patch numbers.
  - Rejects incompatible clients before state synchronization (`RejectedMismatch`, `RejectedMissingMod`, `RejectedOutdatedCore`) with detailed diagnostic messages.
- **Client Memory Config Locking (`DreamLiveConfig.hpp`):**
  - Added `isServerEnforced` and `localSavedValueStr` to `ConfigProperty`.
  - `DreamLiveConfig::LockByServer(key, value)` enforces server-side properties in client memory and fires `OnChange` callbacks.
  - Any local client mutation or console override attempting to tamper with server-locked settings is rejected and logged.
  - Disconnecting from the Dedicated Server (`UnlockAllServerConfigs`) automatically unlocks properties and restores client local values.
- **Decoupled Synchronization Architecture:**
  - Decoupled `DreamServerSync` and `DreamLiveConfig` via `RegisterConfigLockHandler`, maintaining zero circular include dependencies.

### 🧪 Test Suite & ABI Verification
- **Test Suite Expansion:**
  - Added Test 95 covering semantic version comparisons, multi-case Handshake evaluation (Core, Missing, Mismatch, Accepted), memory config locking, cheat override rejection, and disconnect restoration.
  - **All 95 tests passing 100% Green**.
- **ABI Golden Verification:**
  - Passed `check_exports.py` with 131 official API symbols and 0 unlisted exports.
  - Passed `golden_host_test.exe` binary compatibility test (`smoke=0`).

---

## [v3.9.0 v1.3] - 2026-10-31 (Harmony State Sharing, SIMD Scanner & Annotated Disasm Dump)

### 🛡️ Hooking & Instruction-Level Diagnostics
- **Harmony State Sharing (`__state`) (`DreamHarmony.hpp`):**
  - Implemented `DreamPatchState` zero-allocation scratch context passed seamlessly between `Prefix` and `Postfix`.
  - Prefix can save state via `state.Set<T>(val)` and Postfix retrieves it via `state.Get<T>()`.
  - Fully backward compatible: existing patches without `DreamPatchState&` continue to run unmodified.
- **Annotated Disassembly Dumper on Hook Failure (`DreamDisasm.hpp`, `DreamHook.hpp`):**
  - Added `DreamDisasm::DumpAnnotatedDisassembly` which produces an annotated machine-code trace (`[CALL]`, `[TERM]`, `[JCC]`, `[NOP]`) and highlights the exact collision offset and reason.
  - Automatically logged when InlineHook or MidHook installation fails, accelerating patch maintenance across game updates.

### ⚡ Vectorized Pattern Scanning
- **AVX2 256-bit SIMD Pattern Scanner Verification (`DreamScanner.hpp`):**
  - Verified 32-byte/cycle scanning using AVX2 `_mm256_cmpeq_epi8` with automatic SSE2 16-byte fallback, achieving 1-2ms cold boot scans across the entire game module.

### 🧪 Test Suite & ABI Verification
- **Test Suite Expansion:**
  - Added Test 94 covering Harmony State Sharing context transfer, SIMD pattern matching with wildcards, and annotated disassembly dump formatting.
  - **All 94 tests passing 100% Green**.
- **ABI Golden Verification:**
  - Passed `check_exports.py` with 131 official API symbols and 0 unlisted exports.
  - Passed `golden_host_test.exe` binary compatibility test (`smoke=0`).

---

## [v3.9.0 v1.2] - 2026-10-31 (Instruction-Level Safety, Unwind Guard & Concurrency Audit)

### 🛡️ Instruction-Level Relocation & Hook Hardening
- **Universal Stolen `CALL` Instruction Rejection (`DreamHook.hpp`, `DreamDisasm.hpp`):**
  - Completely reject any hook installation attempting to displace `CALL` instructions (`0xE8`, `0xFF /2`, `0xFF /3`) anywhere within the stolen bytes.
  - Eliminates return address corruption, stack unwinder crashes, and prevents the fatal hazard where a simulated return address points into the overwritten 14-byte hook jump stub.
- **Hardware Breakpoint Resume Flag (RF) Isolation (`DreamVeh.hpp`):**
  - Confirmed and verified that `EFlags |= 0x10000` (Resume Flag) is strictly applied **only** to execution breakpoints (`BreakOn::Execute`).
  - Trap-type data watchpoints (`BreakOn::Write`, `BreakOn::ReadWrite`) leave `RF` untouched, preventing suppression of legitimate execution breakpoints on subsequent instructions.

### ⚡ Concurrency & Terraforming Performance
- **Voxel Engine Single-Lock Batching (`DreamVoxelEngine.hpp`):**
  - Moved `std::lock_guard<std::mutex>` outside the 3D loop in `SetTerrainSphere`, eliminating ~17,000 lock/unlock cycles per excavation down to a single lock acquisition.
- **Non-Blocking Store Namespace Loading (`DreamStore.hpp`):**
  - Verified double-checked locking pattern in `GetNamespace`, executing disk I/O (`std::ifstream`) outside `m_masterMutex` to prevent blocking concurrent namespace lookups.
- **Thread Freeze Capacity (`DreamThread.hpp`):**
  - Unified `ScopedThreadFreeze` buffer to `1024` threads (`MAX_FROZEN_THREADS = 1024`), guaranteeing all threads are suspended without truncation on high-core server environments.

### 🧪 Test Suite & ABI Verification
- **Test Suite Expansion:**
  - Added Test 93 covering `ScopedThreadFreeze::TranslateIp` exact & intra-instruction relocation, `DreamVoxelEngine::SetTerrainSphere` batched single-lock terraforming, and `DreamDisasm::IsCall` instruction coverage.
  - **All 93 tests passing 100% Green**.
- **ABI Golden Verification:**
  - Passed `check_exports.py` with 131 official API symbols and 0 unlisted exports.
  - Passed `golden_host_test.exe` binary compatibility test (`smoke=0`).

---

## [v3.9.0 v1.1] - 2026-10-31 (Full Codebase Function Audit, Bug Fixes & ABI Hardening)

### 🐛 Critical Bug Fixes
- **`ConfigEntry<T>::OnChange` Callback Propagation (`DreamLiveConfig.hpp`):**
  - Resolved subtle callback loss where `Bind()` returned a copied `ConfigEntry<T>` whose local callbacks were never invoked.
  - Migrated `m_callbacks` to `std::shared_ptr<std::vector<std::function<void(const T&)>>>`, ensuring all copies share the active callback registry.
  - Eliminated duplicate `NotifyChanged()` invocations in `ConfigEntry<T>::Set` (which was previously firing callbacks twice per update).
- **Task Loss on Time Budget Exhaustion (`DreamScheduler.hpp`):**
  - Resolved critical task dropping bug where `isTimeExhausted()` in `DreamScheduler::Tick` broke early, permanently destroying remaining unexecuted `YieldUntil` and `RunAfter` delayed tasks.
  - All unprocessed coroutines and delayed tasks are now safely preserved and re-queued.
  - Recurring tasks (`RunEvery`) that missed their tick due to time exhaustion now reset their `nextRunAt` to the immediate next frame instead of delaying by an entire extra period.
- **Multi-RVA Telemetry Swallowing & Tag Buffer Fix (`DreamServerTelemetry.hpp`):**
  - Resolved bug in `FlushCoalescedAlerts()` where distinct fault RVAs within the same 250ms window were swallowed and lost. Packets are now cleanly grouped by `(faultRva, faultCode)`.
  - Guaranteed `slot.tag[0] = '\0'` termination when `modTag` is null or empty, preventing stale string bleed in ring buffer slots.
- **`DreamMemory::ReadChain` Boundary Check (`DreamMemory.hpp`):**
  - Added entry guard `if (offsets.empty()) return fallback;` matching `WriteChain`.

### 🛡️ Hooking Safety & Concurrency Hardening
- **Mid-Region `CALL` Relocation Guard (`DreamHook.hpp`, `DreamDisasm.hpp`):**
  - Added `DreamDisasm::IsCall` rejection guard for non-terminal `CALL` instructions (`0xE8`, `0xFF /2`, `0xFF /3`) inside stolen bytes to prevent stack walker and exception unwinder crashes.
- **High-Frequency MidHook Sampling (`DreamHook.hpp`):**
  - Applied sampling (1 in 16 ticks) to `QueryPerformanceCounter` in `DreamChainedHookPipeline::Dispatcher`, eliminating microsecond latency overhead on hot-path hooks.
- **Concurrency Guards (`DreamWatchdog.hpp`, `DreamRpcMultiplexer.hpp`):**
  - Added `m_mutex` guard in `DreamWatchdog::Start` to prevent concurrent thread initialization races.
  - Guarded `m_rawSender` snapshot with `m_mutex` in `DreamRpcMultiplexer::DispatchRpc`.

### 🔒 Public ABI Hardening & Golden Verification
- **Internal Export Trimming (`DreamCExport.hpp`):**
  - Removed `DK_API` from internal policy enforcement (`DK_Policy_*`), server host wiring, and duplicate exports, eliminating unauthorized mod bypass vectors and reducing unlisted exports to 0.
- **ABI Versioning & Context Support (`DreamCExport.hpp`, `dreamkurumiHT_plugin.h`):**
  - Added `DK_GetAbiVersion()` returning `0x00010000`.
  - Added `DK_MidHook2` supporting custom `void* userData`.
  - Added typed plugin bindings for `HashFnv1a64`, player context, console commands, and mod networking.
- **Verification Suite Expansion:**
  - Added Test 91 & Test 92 covering `ConfigEntry::OnChange`, `ReadChain`, `DreamScheduler` time budget preservation, and telemetry multi-RVA coalescing.
  - All 92 unit tests passing (100% Green) + ABI golden test exit code 0 (`smoke=0`).

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
