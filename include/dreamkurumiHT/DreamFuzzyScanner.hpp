#pragma once
#include <windows.h>
#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>
#include <sstream>
#include <iomanip>
#include "DreamScanner.hpp"
#include "DreamLog.hpp"
extern "C" {
#include "hde/hde64.h"
}

namespace dreamkurumi {

// ============================================================================
// DreamFuzzyScanner - Instruction-Aware Multi-Architecture Fuzzy AOB Matcher
// Automatically handles Keen Games compiler updates (stack displacement shifts,
// register re-allocations, relative call/jump offsets) using HDE64 instruction decoding.
// ============================================================================
class DreamFuzzyScanner {
public:
    struct FuzzyInstructionResult {
        std::string fuzzyPattern;
        size_t originalBytesCount{0};
        size_t wildcardBytesCount{0};
        size_t instructionCount{0};
        bool success{false};
    };

    // Converts a strict byte sequence or IDA pattern into a relaxed instruction-aware pattern
    static FuzzyInstructionResult DisassembleAndGenerateFuzzyPattern(const std::vector<uint8_t>& rawBytes, const std::vector<bool>& mask) {
        FuzzyInstructionResult res;
        if (rawBytes.empty()) return res;

        res.originalBytesCount = rawBytes.size();
        std::ostringstream outPattern;

        size_t offset = 0;
        size_t totalLen = rawBytes.size();

        while (offset < totalLen) {
            // Check if current region is masked
            if (offset < mask.size() && !mask[offset]) {
                outPattern << "?? ";
                res.wildcardBytesCount++;
                offset++;
                continue;
            }

            hde64s hs = {0};
            unsigned int len = hde64_disasm(rawBytes.data() + offset, &hs);
            if (len == 0 || (hs.flags & F_ERROR)) {
                // Disassembly failed at this byte, emit byte as-is or ??
                char hex[8];
                snprintf(hex, sizeof(hex), "%02X ", rawBytes[offset]);
                outPattern << hex;
                offset++;
                continue;
            }

            // Ensure instruction does not exceed remaining buffer
            if (offset + len > totalLen) {
                len = static_cast<unsigned int>(totalLen - offset);
            }

            res.instructionCount++;

            // Calculate where prefixes, opcode, modrm, disp, imm are
            size_t dispOffset = 0;
            size_t dispLen = 0;
            if (hs.flags & F_DISP8) { dispLen = 1; }
            else if (hs.flags & F_DISP16) { dispLen = 2; }
            else if (hs.flags & F_DISP32) { dispLen = 4; }

            size_t immOffset = 0;
            size_t immLen = 0;
            if (hs.flags & F_IMM8) { immLen = 1; }
            else if (hs.flags & F_IMM16) { immLen = 2; }
            else if (hs.flags & F_IMM32) { immLen = 4; }
            else if (hs.flags & F_IMM64) { immLen = 8; }

            // In x86_64: disp comes before imm
            if (dispLen > 0) {
                dispOffset = len - immLen - dispLen;
            }
            if (immLen > 0) {
                immOffset = len - immLen;
            }

            // Emit instruction bytes with fuzzy masking
            for (size_t b = 0; b < len; ++b) {
                size_t currentByteIdx = offset + b;
                bool isWildcard = false;

                // 1. Relative calls / jumps (E8 / E9 / 0F 8X) -> Wildcard displacement
                if (hs.flags & F_RELATIVE) {
                    if (b >= (len - 4)) {
                        isWildcard = true; // Relative target offset
                    }
                }
                // 2. Stack or base displacement shifts ([rsp+0x20] -> [rsp+0x28])
                else if (dispLen > 0 && b >= dispOffset && b < dispOffset + dispLen) {
                    isWildcard = true;
                }
                // 3. Stack frame size adjustments (sub rsp, imm8/32 or add rsp, imm8/32)
                else if ((hs.opcode == 0x83 || hs.opcode == 0x81) && (hs.modrm == 0xEC || hs.modrm == 0xC4) && b >= immOffset) {
                    isWildcard = true;
                }
                // 4. Pointer-like 32-bit/64-bit immediates that shift across game builds
                else if (immLen >= 4 && b >= immOffset) {
                    isWildcard = true;
                }

                if (isWildcard) {
                    outPattern << "?? ";
                    res.wildcardBytesCount++;
                } else {
                    char hex[8];
                    snprintf(hex, sizeof(hex), "%02X ", rawBytes[currentByteIdx]);
                    outPattern << hex;
                }
            }

            offset += len;
        }

        std::string patternStr = outPattern.str();
        if (!patternStr.empty() && patternStr.back() == ' ') {
            patternStr.pop_back();
        }

        res.fuzzyPattern = patternStr;
        res.success = !res.fuzzyPattern.empty();
        return res;
    }

    // Generate fuzzy pattern directly from an existing IDA-style pattern string
    static FuzzyInstructionResult FromPatternString(const std::string& patternStr) {
        auto parsed = DreamScanner::ParsePattern(patternStr);
        if (!parsed.isValid) return {};
        return DisassembleAndGenerateFuzzyPattern(parsed.bytes, parsed.mask);
    }

    // Performs an instruction-aware fuzzy scan across module memory or a buffer
    static void* FindFuzzyPattern(const std::string& strictPattern, const void* buffer = nullptr, size_t bufferSize = 0) {
        auto fuzzy = FromPatternString(strictPattern);
        if (!fuzzy.success || fuzzy.fuzzyPattern.empty()) return nullptr;

        DREAM_LOG_DEBUG("DreamFuzzyScanner: Derived fuzzy pattern (%zu instructions, %zu wildcards): %s",
                        fuzzy.instructionCount, fuzzy.wildcardBytesCount, fuzzy.fuzzyPattern.c_str());

        if (buffer && bufferSize > 0) {
            return DreamScanner::FindPattern(fuzzy.fuzzyPattern, buffer, bufferSize);
        } else {
            return DreamScanner::FindPattern(fuzzy.fuzzyPattern.c_str());
        }
    }
};

} // namespace dreamkurumi
