# dreamkurumiHT Technical Architecture & Capabilities

This document contains detailed technical documentation, subsystem architecture, and the automated test verification suite for **dreamkurumiHT**.

---

## 1. Subsystems Overview

### Harmony Hooking API (`DreamHarmony`)
- **Prefix / Postfix Semantics:** Declarative function interception without raw opcode calculation. Returning `false` in `Prefix` skips the original function (`SkipOriginal`).
- **PatchAll & UnpatchAll:** Automatic discovery and batch registration of declared hooks; clean RAII restoration on module unload.
- **Dynamic Register Interception (`DreamMidHook`):** Reads and mutates CPU registers (`RAX`..`R15`, `XMM0`..`XMM15`) directly from C++ without external assembly stubs.
- **Hardware Breakpoints (`DreamVeh`):** Zero-byte patching using CPU debug registers (`DR0`..`DR3`) and Vectored Exception Handling.

### Memory & JIT Trampolines
- **Near-Memory JIT Arena (`DreamTrampolinePool`):** Allocates trampolines within ±2GB relative jump range, preserving CPU cache locality.
- **Thread Freezing & RIP Relocation (`ScopedThreadFreeze`):** Safely suspends running threads during hook installation and translates instruction pointers pointing to displaced bytes.
- **HDE64 Instruction Decoder:** Automatic length decoding, RIP-relative call/jump relocation, and intra-instruction displacement tracking.

### Resilience & Fault Tolerance
- **Kernel CrashGuard (`DreamCrashGuard`):** SEH and VEH guards catching memory faults (0xC0000005) inside callbacks to prevent client/server crashes.
- **Lock-Free Circuit Breaker:** Automatic fault counting per-hook with threshold tripping to safely disable faulty hooks.
- **Session Epoch & Auto-Resync (`DreamSessionSync`):** Detects server restarts and triggers Safe-Mode to prevent accessing stale pointers.

### Networking & RPC Plumbing
- **RPC Multiplexer (`DreamRpcMultiplexer`):** Non-intrusive packet tunnel over the game's transport layer.
- **Rate Limiting & Anti-Spoofing:** Caller ID validation, 120 calls/sec rate limiting, and server-only packet verification.
- **Network State Tracker (`DreamNetStateTracker`):** Tracks GNS connection states, ping, and disconnection events.

### Configuration & Tooling
- **Dual-Standard Config (`DreamToml`):** Full TOML and INI support with live reloading.
- **C-ABI Export Surface (`dreamkurumiHT_plugin.h`):** Stable C-ABI v1 interface for plugins written in C++, C# (.NET Native AOT), or Lua.
- **Zero-Audio Diagnostic HUD:** Optional ImGui diagnostic overlay (disabled by default, configurable via config).

---

## 2. Test Verification Suite (98/98 Green)

The framework is verified using an automated test runner (`test_runner.cpp`) covering all core subsystems:

```text
[TEST 1]  Testing DreamCache (TimeDateStamp & Fast Lookup)           -> [PASS]
[TEST 2]  Testing Baseline Functions (Pre-Hook)                     -> [PASS]
[TEST 3]  Testing Harmony Prefix Skip & Postfix Mutation             -> [PASS]
[TEST 4]  Testing Void Return Function Skip                          -> [PASS]
[TEST 5]  Testing Dynamic Disable & Restore                          -> [PASS]
[TEST 6]  Testing Passive Observer Hooks                             -> [PASS]
[TEST 7]  Testing Pattern Scanning & Fallback                        -> [PASS]
[TEST 8]  Testing DreamHarmony::PatchAll & UnpatchAll                -> [PASS]
[TEST 9]  Testing Memory Patch NOP & Byte Restore                    -> [PASS]
[TEST 10] Testing VTable Method Interception                         -> [PASS]
[TEST 11] Testing Hardware Breakpoint VEH Hook                       -> [PASS]
[TEST 12] Testing DreamInvoker Direct, Member & Virtual Safe Calls    -> [PASS]
[TEST 13] Testing Keen ECS Field Mutation                            -> [PASS]
[TEST 14] Testing Near-Memory JIT Trampoline Arena                   -> [PASS]
[TEST 15] Testing ScopedThreadFreeze & RIP Relocation                -> [PASS]
[TEST 16] Testing Dynamic MSVC RTTI Resolution                       -> [PASS]
[TEST 17] Testing Hook Pipeline Priority Dispatcher                  -> [PASS]
[TEST 18] Testing DreamLog Ring Buffer & HexDump                     -> [PASS]
[TEST 19] Testing Hot-Reloadable TOML Configuration                  -> [PASS]
[TEST 20] Testing Vectored Exception Handling (SEH / VEH)            -> [PASS]
[TEST 21] Testing Lua Script Engine Lifecycle                        -> [PASS]
[TEST 22] Testing Lua Harmony Hook Registration                      -> [PASS]
[TEST 23] Testing Lua EventBus Emission & Reception                  -> [PASS]
[TEST 24] Testing Lua Native MidHook Registers                       -> [PASS]
[TEST 25] Testing Micro-Benchmark Hook Invocation Latency            -> [PASS]
...
[TEST 95] ServerSync Mod Version Handshake Gate & Enforced Configs   -> [PASS]
[TEST 96] NetBufferReader Error Latching & Memory Locks               -> [PASS]
[TEST 97] ServerSessionEpoch & Auto-Resync Safe Mode Protocol        -> [PASS]
[TEST 98] Heartbeat Watchdog Timeout & Zero-Touch Resync             -> [PASS]
```

**Result: 98/98 unit tests passed (100% Green).**
