#pragma once
#include <cstdint>
#include <cstddef>

extern "C" {
#include "hde/hde64.h"
}

namespace dreamkurumi {

struct DecodedInstruction {
    uint8_t length{0};
    uint8_t opcode{0};
    uint8_t opcode2{0};
    bool isRipRelative{false};
    uint8_t dispOffset{0};
    uint8_t dispSize{0};
    int32_t dispVal{0};
    bool isRelativeJump{false};
    uint8_t immOffset{0};
    uint8_t immSize{0};
    int64_t immVal{0};
};

class DreamDisasm {
public:
    static DecodedInstruction Decode(const void* code) {
        DecodedInstruction result{};
        if (!code) return result;

        hde64s hs;
        unsigned int len = hde64_disasm(code, &hs);
        if (hs.flags & F_ERROR) {
            result.length = 0;
            return result;
        }

        result.length = static_cast<uint8_t>(len);
        result.opcode = hs.opcode;
        result.opcode2 = hs.opcode2;

        if (hs.flags & F_IMM8) {
            result.immSize = 1;
            result.immVal = hs.imm.imm8;
            result.immOffset = result.length - 1;
        } else if (hs.flags & F_IMM16) {
            result.immSize = 2;
            result.immVal = hs.imm.imm16;
            result.immOffset = result.length - 2;
        } else if (hs.flags & F_IMM32) {
            result.immSize = 4;
            result.immVal = hs.imm.imm32;
            result.immOffset = result.length - 4;
        } else if (hs.flags & F_IMM64) {
            result.immSize = 8;
            result.immVal = hs.imm.imm64;
            result.immOffset = result.length - 8;
        }

        // Check for RIP-relative addressing (ModR/M with mod = 00 and r/m = 101 [disp32])
        if ((hs.flags & F_MODRM) && (hs.modrm_mod == 0) && (hs.modrm_rm == 5)) {
            result.isRipRelative = true;
            result.dispSize = 4;
            result.dispOffset = result.length - result.immSize - 4;
            result.dispVal = static_cast<int32_t>(hs.disp.disp32);
        }

        if (hs.flags & F_RELATIVE) {
            result.isRelativeJump = true;
        }

        return result;
    }

// ========================================================================
    // Checks if an address starts with an existing hook (E9 / FF 25 / EB)
    // Multi-Loader Coexistence Shield (EML, ShroudForge, Steam/OBS Overlays)
    // ========================================================================
    static bool HasExistingHook(const void* address, uintptr_t* outDest = nullptr) {
        if (!address) return false;
        const uint8_t* p = reinterpret_cast<const uint8_t*>(address);

        // Case 1: 5-byte relative jump (E9 <disp32>)
        if (p[0] == 0xE9) {
            if (outDest) {
                int32_t disp = *reinterpret_cast<const int32_t*>(p + 1);
                *outDest = reinterpret_cast<uintptr_t>(p + 5) + disp;
            }
            return true;
        }

        // Case 2: 6-byte 64-bit indirect jump (FF 25 <disp32>)
        if (p[0] == 0xFF && p[1] == 0x25) {
            if (outDest) {
                int32_t disp = *reinterpret_cast<const int32_t*>(p + 2);
                uintptr_t ptrAddr = reinterpret_cast<uintptr_t>(p + 6) + disp;
                *outDest = *reinterpret_cast<const uintptr_t*>(ptrAddr);
            }
            return true;
        }

        // Case 3: 2-byte short jump (EB <disp8>)
        if (p[0] == 0xEB) {
            if (outDest) {
                int8_t disp = static_cast<int8_t>(p[1]);
                *outDest = reinterpret_cast<uintptr_t>(p + 2) + disp;
            }
            return true;
        }

        return false;
    }

    // Automatically calculate the exact instruction boundary >= minLength
    // Never cuts an instruction in half!
    static size_t GetBoundary(const void* address, size_t minLength) {
        if (!address) return 0;

        const uint8_t* p = reinterpret_cast<const uint8_t*>(address);
        size_t totalBytes = 0;

        while (totalBytes < minLength) {
            hde64s hs;
            unsigned int len = hde64_disasm(p + totalBytes, &hs);
            if (len == 0 || (hs.flags & F_ERROR)) {
                return 0; // Disassembly error
            }
            totalBytes += len;
        }

        return totalBytes;
    }
};

} // namespace dreamkurumi
