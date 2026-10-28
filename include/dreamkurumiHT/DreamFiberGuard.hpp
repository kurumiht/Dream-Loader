#pragma once
#include <windows.h>
#include <intrin.h>
#include <atomic>
#include <cstdint>

namespace dreamkurumi {

// ============================================================================
// ScopedFiberTebGuard - Anti-Crash Guard for Enshrouded Custom Worker Fibers
// Ensures TEB StackBase/StackLimit encompass the active RSP during custom
// user-space fiber execution so frame-based SEH (__try/__except) inside
// DispatchSafeCallback never triggers STATUS_BAD_STACK (0xC0000028).
// Uses a 64-entry Lock-Free Direct-Mapped Cache to reduce VirtualQuery syscall
// overhead from ~800 ns down to ~2 ns (L1 cache hit) in high-frequency hot paths.
// ============================================================================
struct CachedFiberStackSlot {
    std::atomic<uintptr_t> limit{0};
    std::atomic<uintptr_t> base{0};
};

class ScopedFiberTebGuard {
    // Lock-free direct-mapped cache indexed by 64KB stack region hash
    static inline CachedFiberStackSlot s_cache[64]{};

    uintptr_t m_origBase  = 0;
    uintptr_t m_origLimit = 0;
    bool      m_patched   = false;

public:
    __forceinline ScopedFiberTebGuard() noexcept {
#if defined(_MSC_VER)
        const uintptr_t rsp = reinterpret_cast<uintptr_t>(_AddressOfReturnAddress());
#else
        void* localFrame = nullptr;
        const uintptr_t rsp = reinterpret_cast<uintptr_t>(&localFrame);
#endif
        const uintptr_t tebBase  = __readgsqword(0x08); // NT_TIB::StackBase
        const uintptr_t tebLimit = __readgsqword(0x10); // NT_TIB::StackLimit

        // If RSP is outside TEB bounds, we are executing on a custom user-space fiber stack
        if (rsp < tebLimit || rsp >= tebBase) {
            const size_t idx = ((rsp >> 16) ^ (rsp >> 22)) & 63;
            uintptr_t cLimit = s_cache[idx].limit.load(std::memory_order_relaxed);
            uintptr_t cBase  = s_cache[idx].base.load(std::memory_order_relaxed);

            // Cache miss or hash collision: query OS once and populate lock-free slot
            if (rsp < cLimit || rsp >= cBase) {
                MEMORY_BASIC_INFORMATION mbi{};
                if (VirtualQuery(reinterpret_cast<LPCVOID>(rsp), &mbi, sizeof(mbi))) {
                    cLimit = reinterpret_cast<uintptr_t>(mbi.BaseAddress);
                    cBase  = cLimit + mbi.RegionSize;
                    s_cache[idx].limit.store(cLimit, std::memory_order_relaxed);
                    s_cache[idx].base.store(cBase, std::memory_order_relaxed);
                }
            }

            if (cBase > cLimit) {
                m_origBase  = tebBase;
                m_origLimit = tebLimit;
                m_patched   = true;
                __writegsqword(0x08, cBase);
                __writegsqword(0x10, cLimit);
            }
        }
    }

    __forceinline ~ScopedFiberTebGuard() noexcept {
        if (m_patched) {
            __writegsqword(0x08, m_origBase);
            __writegsqword(0x10, m_origLimit);
        }
    }

    bool IsPatched() const noexcept { return m_patched; }
    uintptr_t OrigBase() const noexcept { return m_origBase; }
    uintptr_t OrigLimit() const noexcept { return m_origLimit; }

    static void ClearCache() noexcept {
        for (auto& slot : s_cache) {
            slot.limit.store(0, std::memory_order_relaxed);
            slot.base.store(0, std::memory_order_relaxed);
        }
    }

    static bool IsCached(uintptr_t sp) noexcept {
        const size_t idx = ((sp >> 16) ^ (sp >> 22)) & 63;
        uintptr_t cLimit = s_cache[idx].limit.load(std::memory_order_relaxed);
        uintptr_t cBase  = s_cache[idx].base.load(std::memory_order_relaxed);
        return (sp >= cLimit && sp < cBase && cBase > cLimit);
    }

    // Non-copyable, non-movable
    ScopedFiberTebGuard(const ScopedFiberTebGuard&) = delete;
    ScopedFiberTebGuard& operator=(const ScopedFiberTebGuard&) = delete;
};

} // namespace dreamkurumi

namespace dreamkurumiHT {
    using namespace dreamkurumi;
}
