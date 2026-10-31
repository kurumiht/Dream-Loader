#pragma once
#include <windows.h>
#include <cstdint>
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

// Known loop recovery rules for Enshrouded 1.0 (Server & Client)
static inline const KnownSiteRecoveryRule s_knownServerLoopRules[] = {
    // 1. interaction_attempt batch loop (0xA1394 -> continue label 0xA1C1E)
    { 0x000A1394, 0x000A1C1E, "Server: interaction_attempt batch loop" }
};

static inline const KnownSiteRecoveryRule s_knownClientLoopRules[] = {
    // 1. interaction_attempt batch loop (0x2A3034 -> continue label 0x2A38BE)
    { 0x002A3034, 0x002A38BE, "Client: interaction_attempt batch loop" }
};

inline bool TryRecoverEngineBatchIteration(
    PEXCEPTION_POINTERS pEx,
    uintptr_t moduleBase) noexcept
{
    if (!pEx || !pEx->ExceptionRecord || !pEx->ContextRecord) return false;
    if (pEx->ExceptionRecord->ExceptionCode != EXCEPTION_ACCESS_VIOLATION) return false;

    auto* c = pEx->ContextRecord;
    const uintptr_t currentRva = c->Rip - moduleBase;

    // Check Server known loop rules
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
