#pragma once

// ============================================================================
// dreamkurumiHT - Modern Native Harmony Hooking Framework for C++20
//
// Features:
// 1. Declarative Harmony-Style API (Prefix, Postfix, Skip Original)
// 2. Automated Harmony.PatchAll() and UnpatchAll() Registry
// 3. Hardware Breakpoint & VEH Engine (Zero-byte patching, Zero-RAM scanning)
// 4. Direct Game Function Invoker (DreamInvoker)
// 5. Structural ECS Reflection Engine (DreamReflection)
// 6. Automated Instruction Boundary Disassembly (Zero hardcoded byte counts)
// 7. Dynamic Mid-Function Register Hooks (Zero assembly files needed)
// 8. Build-Aware Symbol & Offset Cache with Auto-Scan Fallback
// 9. Direct Memory Patching & NOP Transpiler (DreamMemoryPatch)
// 10. Virtual Method Table Interception (DreamVTableHook)
// 11. Unified Diagnostics Logger (DreamLog)
// 12. Universal C ABI Exports (C#, Lua, Python, Rust, Zig FFI)
// ============================================================================

#include "DreamCore.hpp"
#include "DreamUnwind.hpp"
#include "DreamFiberGuard.hpp"
#include "DreamVulkanTracker.hpp"
#include "DreamPatcher.hpp"
#include "DreamRelocator.hpp"
#include "DreamPool.hpp"
#include "DreamThread.hpp"
#include "DreamCrashGuard.hpp"
#include "DreamMemory.hpp"
#include "DreamDisasm.hpp"
#include "DreamHook.hpp"
#include "DreamRtti.hpp"
#include "DreamPipeline.hpp"
#include "DreamDispatcher.hpp"
#include "DreamScanner.hpp"
#include "DreamFuzzyScanner.hpp"
#include "DreamCache.hpp"
#include "DreamLog.hpp"
#include "DreamTranspiler.hpp"
#include "DreamWeaver.hpp"
#include "DreamVTable.hpp"
#include "DreamVeh.hpp"
#include "DreamInvoker.hpp"
#include "DreamReflection.hpp"
#include "DreamHarmony.hpp"
#include "DreamPresets.hpp"
#include "DreamTracer.hpp"
#include "DreamOverlay.hpp"
#include "DreamWatcher.hpp"
#include "DreamProfiler.hpp"
#include "DreamVulkan.hpp"
#include "DreamVulkanLayer.hpp"
#include "DreamChainLoader.hpp"
#include "DreamAssetBridge.hpp"
#include "DreamScheduler.hpp"
#include "DreamCrashDump.hpp"
#include "DreamHeuristic.hpp"
#include "DreamEnv.hpp"
#include "DreamHash.hpp"
#include "DreamWatchdog.hpp"
#include "DreamNetBridge.hpp"
#include "DreamGameContext.hpp"
#include "DreamJson.hpp"
#include "DreamEventBus.hpp"
#include "DreamLua.hpp"
#include "DreamEnshroudedTypes.hpp"
#include "DreamToml.hpp"
#include "DreamRpcMultiplexer.hpp"
#include "DreamPlayerRegistry.hpp"
#include "DreamBackup.hpp"
#include "DreamLiveConfig.hpp"
#include "DreamToast.hpp"
#include "DreamStore.hpp"
#include "DreamDiagnosticOverlay.hpp"
#include "DreamPatchMatcher.hpp"
#include "DreamVoxelEngine.hpp"
#include "DreamTrace.hpp"
#include "DreamRttiScan.hpp"
#include "DreamVkOverlay.hpp"
#include "EnshroudedSDK.hpp"
#include "DreamPluginGate.hpp"
#include "DreamGnsTransportHook.hpp"
#include "DreamRawLog.hpp"
#include "DreamEventContext.hpp"
#include "DreamLoopRecovery.hpp"
#include "DreamServerTelemetry.hpp"

namespace dreamkurumi {
namespace detail {
    inline struct DreamToastGuardBridgeInit {
        DreamToastGuardBridgeInit() {
            DreamCrashGuard::SetToastCallback([](const char* msg, float dur, uint32_t col) {
                DreamToast::Instance().Show(msg, dur, col);
            });
        }
    } s_dreamToastGuardBridgeInit;

    inline struct DreamLoopRecoveryBridgeInit {
        DreamLoopRecoveryBridgeInit() {
            DreamCrashGuard::SetLoopRecoveryCallback([](PEXCEPTION_POINTERS pEx) -> bool {
                uintptr_t mainBase = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
                if (!pEx || !pEx->ContextRecord) return false;
                uint32_t faultRva = static_cast<uint32_t>(pEx->ContextRecord->Rip - mainBase);
                if (TryRecoverEngineBatchIteration(pEx, mainBase)) {
                    DreamServerTelemetry::Instance().PushRecoveredFault(
                        pEx->ExceptionRecord ? pEx->ExceptionRecord->ExceptionCode : 0xC0000005,
                        faultRva, "EngineLoop", 1);
                    return true;
                }
                return false;
            });
        }
    } s_dreamLoopRecoveryBridgeInit;
}
}
