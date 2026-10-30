#pragma once
#include <windows.h>
#include <cstdint>
#include <climits>
#include <cstring>

namespace dreamkurumi {

// Emits a 14-byte absolute jump: FF 25 00 00 00 00 [64-bit destination]
// Safe across any 64-bit address range (> 2GB external overlay DLLs like Steam, Discord, OBS)
inline size_t EmitAbsoluteJump64(uint8_t* dst, uintptr_t targetAddress) {
    if (!dst) return 0;
    dst[0] = 0xFF;
    dst[1] = 0x25;
    *reinterpret_cast<uint32_t*>(dst + 2) = 0; // RIP + 0 points directly to dst + 6
    *reinterpret_cast<uint64_t*>(dst + 6) = static_cast<uint64_t>(targetAddress);
    return 14;
}

// Emits a conditional jump relocated (70..7F -> 0F 80..8F or inverted Jcc over 14-byte jmp)
inline size_t EmitConditionalJumpRelocated(uint8_t* dst, uint8_t condNib, uintptr_t destAddr) {
    if (!dst) return 0;
    uintptr_t nextInstr = reinterpret_cast<uintptr_t>(dst + 6);
    int64_t disp = static_cast<int64_t>(destAddr) - static_cast<int64_t>(nextInstr);

    // If within +-2GB, emit standard 6-byte 0F 8x rel32
    if (disp >= INT32_MIN && disp <= INT32_MAX) {
        dst[0] = 0x0F;
        dst[1] = static_cast<uint8_t>(0x80 | (condNib & 0x0F));
        *reinterpret_cast<int32_t*>(dst + 2) = static_cast<int32_t>(disp);
        return 6;
    }

    // Fallback if > 2GB: Invert condition (condNib ^ 1) to skip over 14-byte absolute jump
    dst[0] = static_cast<uint8_t>(0x70 | ((condNib & 0x0F) ^ 0x01));
    dst[1] = 14; // Skip 14 bytes if inverted condition is true
    EmitAbsoluteJump64(dst + 2, destAddr);
    return 16;
}

// Handles existing E9 / FF 25 / EB F9 hooks placed by Steam, Discord, or OBS overlays
inline bool TryChainExistingOverlayHook(
    const uint8_t* target,
    uint8_t* trampoline,
    size_t& outCurOffset,
    size_t& outTrampOffset,
    bool& outReachedTerminator)
{
    if (!target || !trampoline) return false;

    // Case 1: Existing 5-byte relative jump (E9 rel32)
    if (target[0] == 0xE9) {
        int32_t oldRel = *reinterpret_cast<const int32_t*>(target + 1);
        uintptr_t prevHookDest = reinterpret_cast<uintptr_t>(target + 5) + oldRel;

        // Always emit 14-byte absolute jump in trampoline because the previous overlay DLL
        // (e.g., GameOverlayRenderer64.dll) may reside > 2GB away from our DreamTrampolinePool!
        outTrampOffset += EmitAbsoluteJump64(trampoline + outTrampOffset, prevHookDest);
        outCurOffset   += 5;
        outReachedTerminator = true; // Stop decoding stolen bytes immediately!
        return true;
    }

    // Case 2: Existing 6-byte RIP-relative indirect jump (FF 25 disp32)
    if (target[0] == 0xFF && target[1] == 0x25) {
        int32_t disp = *reinterpret_cast<const int32_t*>(target + 2);
        uintptr_t ptrAddress = reinterpret_cast<uintptr_t>(target + 6) + disp;
        uintptr_t prevHookDest = *reinterpret_cast<const uintptr_t*>(ptrAddress);

        outTrampOffset += EmitAbsoluteJump64(trampoline + outTrampOffset, prevHookDest);
        outCurOffset   += 6;
        outReachedTerminator = true;
        return true;
    }

    return false;
}

} // namespace dreamkurumi

namespace dreamkurumiHT {
    using namespace dreamkurumi;
}
