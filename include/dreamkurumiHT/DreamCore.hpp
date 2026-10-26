#pragma once
#include <windows.h>
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <string>
#include <vector>
#include <algorithm>

namespace dreamkurumi {

// ============================================================================
// Scoped VirtualProtect (RAII)
// ============================================================================
class ScopedMemoryProtect {
public:
    ScopedMemoryProtect(void* address, size_t size, DWORD newProtect)
        : m_address(address), m_size(size), m_success(false) {
        if (m_address && m_size > 0) {
            m_success = VirtualProtect(m_address, m_size, newProtect, &m_oldProtect) != FALSE;
        }
    }

    ~ScopedMemoryProtect() {
        if (m_success) {
            DWORD temp;
            VirtualProtect(m_address, m_size, m_oldProtect, &temp);
            FlushInstructionCache(GetCurrentProcess(), m_address, m_size);
        }
    }

    bool IsSuccess() const { return m_success; }

private:
    void* m_address;
    size_t m_size;
    DWORD m_oldProtect{0};
    bool m_success{false};
};

// ============================================================================
// Memory Allocation near Target (within +/- 2GB for relative 32-bit jumps)
// ============================================================================
class MemoryHelper {
public:
    static bool IsValidPtr(const void* ptr) {
        if (!ptr) return false;
        uintptr_t addr = reinterpret_cast<uintptr_t>(ptr);
        return (addr >= 0x10000 && addr < 0x00007FFFFFF00000ULL);
    }

    static void* AllocateNearTarget(void* target, size_t size = 4096) {
        if (!target) return nullptr;

        SYSTEM_INFO si;
        GetSystemInfo(&si);

        uintptr_t targetAddr = reinterpret_cast<uintptr_t>(target);
        uintptr_t minApp = reinterpret_cast<uintptr_t>(si.lpMinimumApplicationAddress);
        uintptr_t maxApp = reinterpret_cast<uintptr_t>(si.lpMaximumApplicationAddress);

        const uintptr_t gran = static_cast<uintptr_t>(si.dwAllocationGranularity ? si.dwAllocationGranularity : 0x10000);
        const uintptr_t mask = ~(gran - 1);

        uintptr_t minAddr = (targetAddr > 0x70000000ULL + minApp) ? (targetAddr - 0x70000000ULL) : minApp;
        uintptr_t maxAddr = (targetAddr + 0x70000000ULL < maxApp) ? (targetAddr + 0x70000000ULL) : maxApp;

        minAddr = (minAddr + gran - 1) & mask;
        maxAddr = maxAddr & mask;

        // 1. Search upwards (closest to target)
        uintptr_t curr = (targetAddr + gran - 1) & mask;
        while (curr < maxAddr) {
            MEMORY_BASIC_INFORMATION mbi;
            if (VirtualQuery(reinterpret_cast<void*>(curr), &mbi, sizeof(mbi)) == 0) {
                curr += gran;
                continue;
            }

            if (mbi.State == MEM_FREE && mbi.RegionSize >= size) {
                void* mem = VirtualAlloc(reinterpret_cast<void*>(curr), size, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
                if (mem) return mem;
            }

            uintptr_t next = reinterpret_cast<uintptr_t>(mbi.BaseAddress) + mbi.RegionSize;
            next = (next + gran - 1) & mask;
            if (next <= curr) next = curr + gran;
            curr = next;
        }

        // 2. Search downwards (closest to target)
        curr = targetAddr > gran ? (targetAddr - gran) & mask : 0;
        while (curr >= minAddr && curr > 0) {
            MEMORY_BASIC_INFORMATION mbi;
            if (VirtualQuery(reinterpret_cast<void*>(curr), &mbi, sizeof(mbi)) == 0) {
                if (curr < gran) break;
                curr -= gran;
                continue;
            }

            if (mbi.State == MEM_FREE && mbi.RegionSize >= size) {
                void* mem = VirtualAlloc(reinterpret_cast<void*>(curr), size, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
                if (mem) return mem;
            }

            uintptr_t base = reinterpret_cast<uintptr_t>(mbi.BaseAddress);
            if (base < gran || base <= minAddr) break;
            curr = (base - gran) & mask;
        }

        return nullptr;
    }

    static void FlushCache(void* address, size_t size) {
        FlushInstructionCache(GetCurrentProcess(), address, size);
    }
};

} // namespace dreamkurumi
