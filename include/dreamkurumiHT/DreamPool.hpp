#pragma once
#include <windows.h>
#include <cstdint>
#include <cstddef>
#include <vector>
#include <mutex>
#include "DreamCore.hpp"
#include "DreamLog.hpp"

namespace dreamkurumi {

// ============================================================================
// Near-Memory Trampoline Pool (JIT Arena)
// Eliminates VirtualAlloc 64KB page waste and keeps CPU instruction cache hot.
// ============================================================================
struct TrampolineChunk {
    uint8_t* baseAddress{nullptr};
    size_t totalSize{0};
    size_t usedBytes{0};
};

class DreamTrampolinePool {
public:
    static DreamTrampolinePool& Instance() {
        static DreamTrampolinePool s_instance;
        return s_instance;
    }

    // Allocate executable memory near target address (within +/- 2GB for rel32 jumps)
    void* Allocate(void* target, size_t size, size_t alignment = 16) {
        if (!target || size == 0) return nullptr;

        std::lock_guard<std::mutex> lock(m_mutex);
        uintptr_t targetAddr = reinterpret_cast<uintptr_t>(target);

        // 1. Search existing chunks in the pool within +/- 1.8GB of target
        for (auto& chunk : m_chunks) {
            uintptr_t chunkAddr = reinterpret_cast<uintptr_t>(chunk.baseAddress);
            int64_t diff = static_cast<int64_t>(chunkAddr - targetAddr);

            // Margin of +/- 0x70000000 (1.875 GB) safely within 32-bit relative range
            if (diff >= -0x70000000LL && diff <= 0x70000000LL) {
                size_t alignedOffset = (chunk.usedBytes + (alignment - 1)) & ~(alignment - 1);
                if (alignedOffset + size <= chunk.totalSize) {
                    void* allocPtr = chunk.baseAddress + alignedOffset;
                    chunk.usedBytes = alignedOffset + size;
                    m_totalAllocated += size;
                    DREAM_LOG_DEBUG("DreamPool: Reused existing chunk at 0x%p (offset 0x%zx, requested %zu bytes)",
                                    chunk.baseAddress, alignedOffset, size);
                    return allocPtr;
                }
            }
        }

        // 2. Allocate a new 64KB page near target (Windows allocation granularity)
        constexpr size_t DEFAULT_CHUNK_SIZE = 64 * 1024; // 64 KB
        size_t allocSize = (size > DEFAULT_CHUNK_SIZE) ? ((size + 0xFFFF) & ~0xFFFF) : DEFAULT_CHUNK_SIZE;

        void* newPage = MemoryHelper::AllocateNearTarget(target, allocSize);
        if (!newPage) {
            DREAM_LOG_ERROR("DreamPool: Failed to allocate near-memory chunk for target 0x%p", target);
            return nullptr;
        }

        TrampolineChunk chunk;
        chunk.baseAddress = reinterpret_cast<uint8_t*>(newPage);
        chunk.totalSize = allocSize;
        chunk.usedBytes = size;

        m_chunks.push_back(chunk);
        m_totalAllocated += size;

        DREAM_LOG_INFO("DreamPool: Created new near-memory 64KB arena chunk at 0x%p (Total active chunks: %zu)",
                       newPage, m_chunks.size());

        return chunk.baseAddress;
    }

    size_t GetChunkCount() const {
        return m_chunks.size();
    }

    size_t GetTotalAllocatedBytes() const {
        return m_totalAllocated;
    }

    void ReleaseAll() {
        std::lock_guard<std::mutex> lock(m_mutex);
        for (auto& chunk : m_chunks) {
            if (chunk.baseAddress) {
                VirtualFree(chunk.baseAddress, 0, MEM_RELEASE);
            }
        }
        m_chunks.clear();
        m_totalAllocated = 0;
    }

private:
    DreamTrampolinePool() = default;
    ~DreamTrampolinePool() {
        ReleaseAll();
    }

    std::mutex m_mutex;
    std::vector<TrampolineChunk> m_chunks;
    size_t m_totalAllocated{0};
};

} // namespace dreamkurumi
