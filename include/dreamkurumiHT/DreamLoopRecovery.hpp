#pragma once
#include <windows.h>
#include <cstdint>
#include "DreamCore.hpp"
#include "DreamEnv.hpp"
#include "DreamAutoLoopResolver.hpp"

namespace dreamkurumi {

// ============================================================================
// DreamLoopRecovery - Known-Site Recovery Rules for Keen Holistic Engine Loops
// Safely advances loop iterator latch on fault, preserving engine locks,
// stack frame, and scratch arena buffers (Dev 2 Architecture).
// ============================================================================

struct KnownSiteRecoveryRule {
    uintptr_t faultRva;    // Exact RVA of instruction that could fault on null/despawned entity
    uintptr_t continueRva; // Exact RVA of loop continue / latch increment label
    const char* loopName;
};

// Known loop recovery rules for Enshrouded 1.0 & 0.0.15.0 (Server & Client)
static inline const KnownSiteRecoveryRule s_knownServerLoopRules[] = {
    // 1. interaction_attempt batch loop - 0.0.15.0 Server (0xA5024 -> continue label 0xA58AE)
    { 0x000A5024, 0x000A58AE, "Server: interaction_attempt batch loop (0.0.15.0)" },
    // 2. interaction_attempt batch loop - 1.0 Legacy Server (0xA1394 -> continue label 0xA1C1E)
    { 0x000A1394, 0x000A1C1E, "Server: interaction_attempt batch loop (1.0 Legacy)" }
};

static inline const KnownSiteRecoveryRule s_knownClientLoopRules[] = {
    // 1. interaction_attempt batch loop - 0.0.15.0 Client (0x2A7AD4 -> continue label 0x2A835E)
    { 0x002A7AD4, 0x002A835E, "Client: interaction_attempt batch loop (0.0.15.0)" },
    // 2. interaction_attempt batch loop - 1.0 Legacy Client (0x2A3034 -> continue label 0x2A38BE)
    { 0x002A3034, 0x002A38BE, "Client: interaction_attempt batch loop (1.0 Legacy)" }
};

inline bool TryRecoverEngineBatchIteration(
    PEXCEPTION_POINTERS pEx,
    uintptr_t moduleBase) noexcept
{
    if (!pEx || !pEx->ExceptionRecord || !pEx->ContextRecord) return false;
    if (pEx->ExceptionRecord->ExceptionCode != EXCEPTION_ACCESS_VIOLATION) return false;

    auto* c = pEx->ContextRecord;
    const uintptr_t currentRva = c->Rip - moduleBase;

    // 1. Direct O(1) Self-Healing Loop Recovery (Dev 2 Architecture)
    // The fault site is cmp byte ptr [rcx+2Dh], 0 (80 79 2D 00), which is always preceded by:
    // 48 85 C9       test rcx, rcx
    // 0F 84 <rel32>  je <continue_label>
    // The relative jump displacement rel32 points directly to the loop latch / continue label!
    if (MemoryHelper::IsCommittedAndReadable(reinterpret_cast<const void*>(c->Rip - 9), 13)) {
        const uint8_t* ripBytes = reinterpret_cast<const uint8_t*>(c->Rip);
        if (ripBytes[0] == 0x80 && ripBytes[1] == 0x79 && ripBytes[2] == 0x2D && ripBytes[3] == 0x00) {
            const uint8_t* prevBytes = ripBytes - 9;
            if (prevBytes[0] == 0x48 && prevBytes[1] == 0x85 && prevBytes[2] == 0xC9 &&
                prevBytes[3] == 0x0F && prevBytes[4] == 0x84)
            {
                const int32_t jmpRel32 = *reinterpret_cast<const int32_t*>(ripBytes - 4);
                if (jmpRel32 > 0 && jmpRel32 < 0x2000) {
                    c->Rip = c->Rip + jmpRel32;
                    c->MxCsr         &= ~0x3FU;
                    c->FltSave.MxCsr &= ~0x3FU;
                    return true;
                }
            }
        }
    }

    // 2. Check Server known loop rules
    for (const auto& rule : s_knownServerLoopRules) {
        if (currentRva == rule.faultRva) {
            c->Rip = moduleBase + rule.continueRva;
            c->MxCsr         &= ~0x3FU;
            c->FltSave.MxCsr &= ~0x3FU;
            return true;
        }
    }

    // Check Client known loop rules
    for (const auto& rule : s_knownClientLoopRules) {
        if (currentRva == rule.faultRva) {
            c->Rip = moduleBase + rule.continueRva;
            c->MxCsr         &= ~0x3FU;
            c->FltSave.MxCsr &= ~0x3FU;
            return true;
        }
    }

    // 2. Dynamic CFG Loop-Latch Auto-Resolver (Works across game updates without hardcoding!)
    uintptr_t autoContinue = DreamAutoLoopResolver::ResolveLoopContinueAddress(c->Rip);
    if (autoContinue != 0) {
        c->Rip = autoContinue;
        c->MxCsr         &= ~0x3FU;
        c->FltSave.MxCsr &= ~0x3FU;
        return true;
    }

    return false;
}

} // namespace dreamkurumi
