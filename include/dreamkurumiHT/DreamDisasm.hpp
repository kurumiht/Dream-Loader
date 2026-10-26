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
