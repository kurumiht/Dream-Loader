#pragma once
#include <windows.h>
#include <cstdint>
#include <vector>
#include <string>
#include <sstream>
#include <iomanip>
#include <cstring>
#include "DreamCore.hpp"
#include "DreamCrashGuard.hpp"
#include "DreamThread.hpp"
#include "DreamLog.hpp"

namespace dreamkurumi {

// ============================================================================
// DreamMemory: Safe Memory Operations, Pointer Chains, and Byte Patching
// ============================================================================
class DreamMemory {
public:
    // Safe memory reading with SEH / CrashGuard protection against Bad Pointers
    template <typename T>
    static T Read(const void* address, T fallback = T{}) {
        if (!MemoryHelper::IsValidPtr(address)) return fallback;
        T result = fallback;
        DreamCrashGuard::ExecuteSafely("MemoryRead", [&]() {
            result = *reinterpret_cast<const T*>(address);
        });
        return result;
    }

    // Torn-read detection for non-atomic concurrent reads (e.g. 4x4 View-Projection matrix <= 64 bytes)
    // Ensures two identical consecutive reads without torn values from background writer threads.
    static bool ReadStable(const void* src, void* dst, size_t n /* <= 64 */) {
        if (!src || !dst || n == 0 || n > 64) return false;
        if (!MemoryHelper::IsValidPtr(src)) return false;
        alignas(16) char tmp[64];
        for (int i = 0; i < 3; ++i) {
            std::memcpy(dst, src, n);
            std::memcpy(tmp, src, n);
            if (!std::memcmp(dst, tmp, n)) return true; // Two identical reads: consistent & not torn
        }
        return false; // Caller keeps last known good snapshot
    }

    // Safe memory writing with smart protection query (avoids syscall if already writable)
    template <typename T>
    static bool Write(void* address, const T& value) {
        if (!MemoryHelper::IsValidPtr(address)) return false;
        bool ok = false;
        DreamCrashGuard::ExecuteSafely("MemoryWrite", [&]() {
            MEMORY_BASIC_INFORMATION mbi{};
            if (VirtualQuery(address, &mbi, sizeof(mbi))) {
                constexpr DWORD WRITABLE_MASK = PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
                if ((mbi.Protect & WRITABLE_MASK) != 0) {
                    // Already writable! Direct write with zero syscall overhead
                    *reinterpret_cast<T*>(address) = value;
                    ok = true;
                    return;
                }

                // Preserve non-executable DEP for data: only use PAGE_EXECUTE_READWRITE if code
                bool isExec = (mbi.Protect & (PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY)) != 0;
                DWORD newProt = isExec ? PAGE_EXECUTE_READWRITE : PAGE_READWRITE;

                ScopedMemoryProtect prot(address, sizeof(T), newProt);
                if (prot.IsSuccess()) {
                    *reinterpret_cast<T*>(address) = value;
                    if (isExec) {
                        MemoryHelper::FlushCache(address, sizeof(T));
                    }
                    ok = true;
                }
            }
        });
        return ok;
    }

    // Multi-level Pointer Chain Resolution: base -> +off1 -> +off2 -> +off3
    // Example: ReadChain<float>(pPlayer, { 0x18, 0x30, 0x48 }, 100.0f)
    template <typename T>
    static T ReadChain(const void* baseAddress, const std::vector<uintptr_t>& offsets, T fallback = T{}) {
        if (!MemoryHelper::IsValidPtr(baseAddress)) return fallback;
        uintptr_t current = reinterpret_cast<uintptr_t>(baseAddress);

        for (size_t i = 0; i < offsets.size(); ++i) {
            if (!MemoryHelper::IsValidPtr(reinterpret_cast<const void*>(current))) {
                return fallback;
            }
            if (i == offsets.size() - 1) {
                return Read<T>(reinterpret_cast<const void*>(current + offsets[i]), fallback);
            } else {
                current = Read<uintptr_t>(reinterpret_cast<const void*>(current + offsets[i]), 0);
                if (current == 0) return fallback;
            }
        }
        return fallback;
    }

    // Multi-level Pointer Chain Write: base -> +off1 -> +off2 -> [target] = value
    template <typename T>
    static bool WriteChain(void* baseAddress, const std::vector<uintptr_t>& offsets, const T& value) {
        if (!MemoryHelper::IsValidPtr(baseAddress) || offsets.empty()) return false;
        uintptr_t current = reinterpret_cast<uintptr_t>(baseAddress);

        for (size_t i = 0; i < offsets.size() - 1; ++i) {
            if (!MemoryHelper::IsValidPtr(reinterpret_cast<const void*>(current))) return false;
            current = Read<uintptr_t>(reinterpret_cast<const void*>(current + offsets[i]), 0);
            if (current == 0) return false;
        }

        return Write<T>(reinterpret_cast<void*>(current + offsets.back()), value);
    }

    // Patch byte sequence with automatic thread freeze and original byte backup
    static bool PatchBytes(void* address, const std::vector<uint8_t>& newBytes, std::vector<uint8_t>* outOriginal = nullptr) {
        if (!address || newBytes.empty()) return false;
        ScopedThreadFreeze freeze(address, newBytes.size());
        ScopedMemoryProtect prot(address, newBytes.size(), PAGE_EXECUTE_READWRITE);
        if (!prot.IsSuccess()) return false;

        if (outOriginal) {
            outOriginal->resize(newBytes.size());
            std::memcpy(outOriginal->data(), address, newBytes.size());
        }

        std::memcpy(address, newBytes.data(), newBytes.size());
        MemoryHelper::FlushCache(address, newBytes.size());
        return true;
    }

    // Patch by hex string (e.g. "90 90 90 90 90")
    static bool PatchHex(void* address, const std::string& hexPattern, std::vector<uint8_t>* outOriginal = nullptr) {
        std::vector<uint8_t> bytes;
        std::istringstream iss(hexPattern);
        std::string byteStr;
        while (iss >> byteStr) {
            try {
                bytes.push_back(static_cast<uint8_t>(std::stoul(byteStr, nullptr, 16)));
            } catch (...) {
                return false;
            }
        }
        return PatchBytes(address, bytes, outOriginal);
    }

    // NOP out memory range
    static bool Nop(void* address, size_t count, std::vector<uint8_t>* outOriginal = nullptr) {
        if (!address || count == 0) return false;
        std::vector<uint8_t> nops(count, 0x90);
        return PatchBytes(address, nops, outOriginal);
    }
};

// ============================================================================
// DreamBytePatch: RAII Reversible Memory Byte Patch
// ============================================================================
class DreamBytePatch {
public:
    DreamBytePatch(void* address, const std::vector<uint8_t>& patchBytes)
        : m_address(address), m_patchBytes(patchBytes), m_applied(false) {}

    DreamBytePatch(void* address, const std::string& hexPattern)
        : m_address(address), m_applied(false) {
        std::istringstream iss(hexPattern);
        std::string byteStr;
        while (iss >> byteStr) {
            try {
                m_patchBytes.push_back(static_cast<uint8_t>(std::stoul(byteStr, nullptr, 16)));
            } catch (...) {}
        }
    }

    ~DreamBytePatch() {
        Restore();
    }

    bool Apply() {
        if (m_applied || !m_address || m_patchBytes.empty()) return false;
        m_applied = DreamMemory::PatchBytes(m_address, m_patchBytes, &m_originalBytes);
        return m_applied;
    }

    bool Restore() {
        if (!m_applied || !m_address || m_originalBytes.empty()) return false;
        bool ok = DreamMemory::PatchBytes(m_address, m_originalBytes);
        if (ok) m_applied = false;
        return ok;
    }

    bool IsApplied() const { return m_applied; }

private:
    void* m_address{nullptr};
    std::vector<uint8_t> m_patchBytes;
    std::vector<uint8_t> m_originalBytes;
    bool m_applied{false};
};

} // namespace dreamkurumi
