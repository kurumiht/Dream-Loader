#pragma once
#include <windows.h>
#include <cstdint>
#include <vector>
#include <string>
#include <cstring>
#include "DreamCore.hpp"
#include "DreamCache.hpp"
#include "DreamThread.hpp"
#include "DreamLog.hpp"

namespace dreamkurumi {

// ============================================================================
// DreamMemoryPatch (Direct Byte Transpiler & NOP Tool)
// ============================================================================
class DreamMemoryPatch {
public:
    DreamMemoryPatch() = default;
    ~DreamMemoryPatch() { Restore(); }

    bool PatchBytes(void* address, const std::vector<uint8_t>& newBytes) {
        if (!address || newBytes.empty()) return false;
        if (m_isApplied) Restore();

        m_address = reinterpret_cast<uint8_t*>(address);
        m_size = newBytes.size();

        // Backup original bytes
        m_originalBytes.resize(m_size);
        std::memcpy(m_originalBytes.data(), m_address, m_size);

        // Freeze worker threads during byte patching
        ScopedThreadFreeze freeze(m_address, m_size);

        // Apply new bytes
        ScopedMemoryProtect protect(m_address, m_size, PAGE_EXECUTE_READWRITE);
        if (!protect.IsSuccess()) return false;

        std::memcpy(m_address, newBytes.data(), m_size);
        MemoryHelper::FlushCache(m_address, m_size);
        m_isApplied = true;

        DREAM_LOG_INFO("Applied memory patch at 0x%p (%zu bytes)", m_address, m_size);
        return true;
    }

    // NOP out instructions cleanly
    bool Nop(void* address, size_t count) {
        if (!address || count == 0) return false;
        std::vector<uint8_t> nopBytes(count, 0x90);
        return PatchBytes(address, nopBytes);
    }

    // Patch by Symbol from Cache
    bool PatchSymbolBytes(const std::string& symbolName, const std::vector<uint8_t>& newBytes, uintptr_t fallbackRva = 0) {
        void* addr = DreamCache::Instance().Resolve(symbolName, fallbackRva);
        if (!addr) {
            DREAM_LOG_ERROR("Failed to resolve symbol '%s' for memory patch", symbolName.c_str());
            return false;
        }
        return PatchBytes(addr, newBytes);
    }

    bool NopSymbol(const std::string& symbolName, size_t count, uintptr_t fallbackRva = 0) {
        std::vector<uint8_t> nopBytes(count, 0x90);
        return PatchSymbolBytes(symbolName, nopBytes, fallbackRva);
    }

    bool Restore() {
        if (!m_address || !m_isApplied) return false;

        // Freeze worker threads during restore
        ScopedThreadFreeze freeze(m_address, m_size);

        ScopedMemoryProtect protect(m_address, m_size, PAGE_EXECUTE_READWRITE);
        if (!protect.IsSuccess()) return false;

        std::memcpy(m_address, m_originalBytes.data(), m_size);
        MemoryHelper::FlushCache(m_address, m_size);
        m_isApplied = false;

        DREAM_LOG_INFO("Restored memory patch at 0x%p", m_address);
        return true;
    }

    bool IsApplied() const { return m_isApplied; }

private:
    uint8_t* m_address{nullptr};
    size_t m_size{0};
    std::vector<uint8_t> m_originalBytes;
    bool m_isApplied{false};
};

} // namespace dreamkurumi
