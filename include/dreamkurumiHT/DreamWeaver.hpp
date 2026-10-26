#pragma once
#include <windows.h>
#include <cstdint>
#include <vector>
#include <string>
#include <functional>
#include "DreamDisasm.hpp"
#include "DreamCore.hpp"
#include "DreamThread.hpp"
#include "DreamLog.hpp"

namespace dreamkurumi {

// ============================================================================
// Instruction Token for Native Dynamic Transpilation
// ============================================================================
struct AsmInstructionToken {
    uint8_t* address{nullptr};
    size_t length{0};
    uint8_t opcode{0};
    uint8_t opcode2{0};
    uint8_t originalBytes[16]{0};
    std::vector<uint8_t> newBytes;
    bool isModified{false};
    DecodedInstruction decoded;

    // Convert this instruction into NOPs (0x90)
    void Nop() {
        newBytes.assign(length, 0x90);
        isModified = true;
    }

    // Replace with custom byte sequence of identical length
    void ReplaceBytes(const std::vector<uint8_t>& replacement) {
        newBytes = replacement;
        isModified = true;
    }

    // Modify immediate value if instruction contains one
    void SetImm32(int32_t newImm) {
        newBytes.assign(originalBytes, originalBytes + length);
        if (decoded.immSize == 4 && decoded.immOffset > 0 && decoded.immOffset + 4 <= length) {
            *reinterpret_cast<int32_t*>(&newBytes[decoded.immOffset]) = newImm;
            isModified = true;
        }
    }
};

// ============================================================================
// InstructionStream - Iterable Stream of x64 Instructions
// ============================================================================
class InstructionStream {
public:
    InstructionStream(void* functionAddress, size_t maxScanBytes = 256)
        : m_base(reinterpret_cast<uint8_t*>(functionAddress)), m_scanLimit(maxScanBytes) {
        Disassemble();
    }

    size_t Size() const { return m_tokens.size(); }

    AsmInstructionToken* Get(size_t index) {
        if (index < m_tokens.size()) return &m_tokens[index];
        return nullptr;
    }

    // Find instruction by opcode (e.g. 0x75 for JNE, 0x74 for JE, 0xE8 for CALL)
    AsmInstructionToken* FindOpcode(uint8_t opcode, size_t startIndex = 0) {
        for (size_t i = startIndex; i < m_tokens.size(); ++i) {
            if (m_tokens[i].opcode == opcode) {
                return &m_tokens[i];
            }
        }
        return nullptr;
    }

    // Find instruction with custom predicate
    template <typename Predicate>
    AsmInstructionToken* Find(Predicate&& pred, size_t startIndex = 0) {
        for (size_t i = startIndex; i < m_tokens.size(); ++i) {
            if (pred(m_tokens[i])) {
                return &m_tokens[i];
            }
        }
        return nullptr;
    }

    // Apply all modified tokens to game memory with thread freezing
    bool Apply() {
        bool anyModified = false;
        for (const auto& tok : m_tokens) {
            if (tok.isModified) {
                anyModified = true;
                break;
            }
        }
        if (!anyModified) return true;

        ScopedThreadFreeze freeze(m_base, m_scanLimit);
        ScopedMemoryProtect protect(m_base, m_scanLimit, PAGE_EXECUTE_READWRITE);
        if (!protect.IsSuccess()) return false;

        for (auto& tok : m_tokens) {
            if (tok.isModified && !tok.newBytes.empty()) {
                std::memcpy(tok.address, tok.newBytes.data(), tok.newBytes.size());
                DREAM_LOG_INFO("DreamWeaver: Modified instruction at 0x%p (%zu bytes)", tok.address, tok.newBytes.size());
            }
        }

        FlushInstructionCache(GetCurrentProcess(), m_base, m_scanLimit);
        m_isApplied = true;
        return true;
    }

    // Restore all instructions to original state
    bool Restore() {
        if (!m_isApplied) return true;

        ScopedThreadFreeze freeze(m_base, m_scanLimit);
        ScopedMemoryProtect protect(m_base, m_scanLimit, PAGE_EXECUTE_READWRITE);
        if (!protect.IsSuccess()) return false;

        for (auto& tok : m_tokens) {
            if (tok.isModified) {
                std::memcpy(tok.address, tok.originalBytes, tok.length);
            }
        }

        FlushInstructionCache(GetCurrentProcess(), m_base, m_scanLimit);
        m_isApplied = false;
        DREAM_LOG_INFO("DreamWeaver: Restored all modified instructions at 0x%p", m_base);
        return true;
    }

    bool IsApplied() const { return m_isApplied; }

private:
    void Disassemble() {
        if (!m_base) return;
        size_t offset = 0;

        while (offset < m_scanLimit) {
            uint8_t* curPtr = m_base + offset;
            auto dec = DreamDisasm::Decode(curPtr);
            if (dec.length == 0 || dec.length > 15) break;

            AsmInstructionToken token;
            token.address = curPtr;
            token.length = dec.length;
            token.opcode = dec.opcode;
            token.opcode2 = dec.opcode2;
            token.decoded = dec;
            std::memcpy(token.originalBytes, curPtr, dec.length);

            m_tokens.push_back(token);
            offset += dec.length;

            // Stop at RET (0xC3) or RET imm16 (0xC2)
            if (dec.opcode == 0xC3 || dec.opcode == 0xC2) {
                break;
            }
        }
    }

    uint8_t* m_base{nullptr};
    size_t m_scanLimit{256};
    std::vector<AsmInstructionToken> m_tokens;
    bool m_isApplied{false};
};

// ============================================================================
// DreamWeaver - Harmony-Style Dynamic Transpiler
// ============================================================================
class DreamWeaver {
public:
    using TranspilerFn = std::function<void(InstructionStream&)>;

    static bool Transpile(void* targetFunction, TranspilerFn transpiler, size_t maxScanBytes = 256) {
        if (!targetFunction || !transpiler) return false;

        InstructionStream stream(targetFunction, maxScanBytes);
        transpiler(stream);
        return stream.Apply();
    }
};

} // namespace dreamkurumi
