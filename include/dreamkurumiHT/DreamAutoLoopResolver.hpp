#pragma once
#include <windows.h>
#include <cstdint>
#include "DreamCore.hpp"
#include "DreamDisasm.hpp"
#include "DreamLog.hpp"

namespace dreamkurumi {

// ============================================================================
// DreamAutoLoopResolver: Dynamic CFG Loop-Latch & Epilogue Recovery Resolver
//
// Analyzes function Control-Flow Graphs (CFG) at runtime using OS .pdata
// (RtlLookupFunctionEntry) and HDE64 instruction decoding.
//
// Automatically discovers the safe loop-latch (continue) target (Delta ~+0x88A)
// across game patches without requiring manual RVA recalculation!
// ============================================================================
class DreamAutoLoopResolver {
public:
    // Dynamically resolves the safe loop-latch (continue) address from a fault site inside a batch loop.
    // Works automatically across game updates without hardcoding the destination RVA!
    static uintptr_t ResolveLoopContinueAddress(uintptr_t faultAddr) noexcept {
        if (!faultAddr) return 0;

        DWORD64 imageBase = 0;
        RUNTIME_FUNCTION* rf = RtlLookupFunctionEntry(faultAddr, &imageBase, nullptr);
        if (!rf || !imageBase) {
            DREAM_LOG_DEBUG("DreamAutoLoopResolver: No RUNTIME_FUNCTION found for 0x%llX",
                            static_cast<unsigned long long>(faultAddr));
            return 0;
        }

        const uintptr_t funcStart = static_cast<uintptr_t>(imageBase + rf->BeginAddress);
        const uintptr_t funcEnd   = static_cast<uintptr_t>(imageBase + rf->EndAddress);

        // Pass 1: Look for an existing compiler-generated guard branch (0F 84 / 0F 85 Jcc rel32)
        // near the fault site [faultAddr - 64, faultAddr + 64] that jumps forward to the loop latch (+0x200..+0x1500)
        const uintptr_t scanNearStart = (faultAddr >= 64 && faultAddr - 64 > funcStart) ? (faultAddr - 64) : funcStart;
        const uintptr_t scanNearEnd   = (faultAddr + 64 < funcEnd) ? (faultAddr + 64) : funcEnd;

        for (uintptr_t p = scanNearStart; p + 6 <= scanNearEnd; ++p) {
            if (!MemoryHelper::IsSafeToProbeAddress(reinterpret_cast<const void*>(p), 6)) continue;
            const uint8_t* code = reinterpret_cast<const uint8_t*>(p);
            // Check for 6-byte near JZ (0F 84) or JNZ (0F 85)
            if (code[0] == 0x0F && (code[1] == 0x84 || code[1] == 0x85)) {
                int32_t disp = *reinterpret_cast<const int32_t*>(code + 2);
                if (disp >= 0x200 && disp <= 0x1500) {
                    uintptr_t targetLatch = p + 6 + disp;
                    if (targetLatch > faultAddr && targetLatch < funcEnd) {
                        // Verify that targetLatch is within 32 bytes of a backward loop branch!
                        if (HasBackwardBranchNear(targetLatch, faultAddr, funcStart, funcEnd)) {
                            DREAM_LOG_INFO("DreamAutoLoopResolver: Auto-resolved loop latch at 0x%llX (Delta: +0x%llX)",
                                           static_cast<unsigned long long>(targetLatch),
                                           static_cast<unsigned long long>(targetLatch - faultAddr));
                            return targetLatch;
                        }
                    }
                }
            }
        }

        // Pass 2: Scan forward in [faultAddr + 0x200, min(funcEnd, faultAddr + 0x1500)] for the loop back-edge
        // jump (E9 or 0F 8x with negative displacement jumping above faultAddr)
        const uintptr_t latchSearchStart = faultAddr + 0x200;
        const uintptr_t latchSearchEnd   = (faultAddr + 0x1500 < funcEnd) ? (faultAddr + 0x1500) : funcEnd;

        uintptr_t cur = faultAddr;
        while (cur < latchSearchEnd) {
            if (!MemoryHelper::IsSafeToProbeAddress(reinterpret_cast<const void*>(cur), 16)) break;
            auto dec = DreamDisasm::Decode(reinterpret_cast<const void*>(cur));
            if (dec.length == 0) {
                cur++;
                continue;
            }
            if (cur >= latchSearchStart) {
                const uint8_t* b = reinterpret_cast<const uint8_t*>(cur);
                uintptr_t backTarget = 0;
                if (b[0] == 0xE9 && dec.length == 5) {
                    int32_t rel = *reinterpret_cast<const int32_t*>(b + 1);
                    if (rel < 0) backTarget = cur + 5 + rel;
                } else if (b[0] == 0x0F && (b[1] >= 0x80 && b[1] <= 0x8F) && dec.length == 6) {
                    int32_t rel = *reinterpret_cast<const int32_t*>(b + 2);
                    if (rel < 0) backTarget = cur + 6 + rel;
                }

                // If this instruction jumps back to the loop header right above faultAddr:
                if (backTarget >= funcStart && backTarget < faultAddr && (faultAddr - backTarget) <= 0x400) {
                    DREAM_LOG_INFO("DreamAutoLoopResolver: Found loop back-edge at 0x%llX -> header 0x%llX",
                                   static_cast<unsigned long long>(cur),
                                   static_cast<unsigned long long>(backTarget));
                    // The latch block begins before the back-edge branch (typically 1-3 instructions prior)
                    return cur;
                }
            }
            cur += dec.length;
        }

        return 0;
    }

private:
    static bool HasBackwardBranchNear(uintptr_t latchAddr, uintptr_t faultAddr, uintptr_t funcStart, uintptr_t funcEnd) noexcept {
        uintptr_t p = latchAddr;
        for (int instrs = 0; instrs < 12 && p < funcEnd; ++instrs) {
            if (!MemoryHelper::IsSafeToProbeAddress(reinterpret_cast<const void*>(p), 16)) break;
            auto dec = DreamDisasm::Decode(reinterpret_cast<const void*>(p));
            if (dec.length == 0) break;
            const uint8_t* b = reinterpret_cast<const uint8_t*>(p);
            if (b[0] == 0xE9 && dec.length == 5) {
                int32_t rel = *reinterpret_cast<const int32_t*>(b + 1);
                uintptr_t dest = p + 5 + rel;
                if (rel < 0 && dest >= funcStart && dest <= faultAddr) return true;
            }
            if (b[0] == 0x0F && (b[1] >= 0x80 && b[1] <= 0x8F) && dec.length == 6) {
                int32_t rel = *reinterpret_cast<const int32_t*>(b + 2);
                uintptr_t dest = p + 6 + rel;
                if (rel < 0 && dest >= funcStart && dest <= faultAddr) return true;
            }
            p += dec.length;
        }
        return false;
    }
};

} // namespace dreamkurumi
