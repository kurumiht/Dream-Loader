#pragma once
#include <windows.h>
#include <cstdint>
#include <cstddef>
#include <vector>
#include <memory>
#include <mutex>
#include "DreamCore.hpp"
#include "DreamLog.hpp"
#include "DreamUnwind.hpp"

namespace dreamkurumi {

// ============================================================================
// Near-Memory Trampoline Pool (JIT Arena)
// Eliminates VirtualAlloc 64KB page waste and keeps CPU instruction cache hot.
// Supports architectural separation between pure Leaf hooks (InlineHook)
// and Framed hooks (MidHook with exact per-stub RUNTIME_FUNCTION entries).
// ============================================================================
enum class ChunkKind {
    InlineLeaf,      // Pure leaf trampolines (no RtlAddFunctionTable; Windows unwinder defaults to RSP-based leaf)
    MidHookFramed    // Framed MidHook stubs with MinimalUnwindInfo (UWOP_SET_FPREG for RBP)
};

struct TrampolineChunk {
    uint8_t* baseAddress{nullptr};
    size_t totalSize{0};
    size_t usedBytes{0};
    ChunkKind kind{ChunkKind::InlineLeaf};
    std::vector<std::unique_ptr<RUNTIME_FUNCTION>> registeredFunctions;
};

class DreamTrampolinePool {
public:
    static DreamTrampolinePool& Instance() {
        static DreamTrampolinePool s_instance;
        return s_instance;
    }

    // Allocate executable memory for pure Leaf functions (InlineHook trampolines and near relays)
    // Zero RtlAddFunctionTable called: Windows x64 unwinder treats dynamic pages without .pdata
    // as RSP-based Leaf functions automatically, preserving stack integrity during unroll!
    void* Allocate(void* target, size_t size, size_t alignment = 16) {
        return AllocateInternal(target, size, alignment, ChunkKind::InlineLeaf);
    }

    // Allocate executable memory for Framed MidHook stubs
    // Maintains exact per-stub RUNTIME_FUNCTION entries so (Rip - BeginAddress) < SizeOfProlog
    // evaluates accurately and stack unwinds restore RBP cleanly.
    void* AllocateMidHook(void* target, size_t size, size_t alignment = 16) {
        return AllocateInternal(target, size, alignment, ChunkKind::MidHookFramed);
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
            for (auto& rf : chunk.registeredFunctions) {
                if (rf) {
                    RtlDeleteFunctionTable(rf.get());
                }
            }
            chunk.registeredFunctions.clear();
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

    void* AllocateInternal(void* target, size_t size, size_t alignment, ChunkKind kind) {
        if (!target || size == 0) return nullptr;

        std::lock_guard<std::mutex> lock(m_mutex);
        uintptr_t targetAddr = reinterpret_cast<uintptr_t>(target);

        // 1. Search existing chunks of the SAME kind in the pool within +/- 1.8GB of target
        for (auto& chunk : m_chunks) {
            if (chunk.kind != kind) continue;

            uintptr_t chunkAddr = reinterpret_cast<uintptr_t>(chunk.baseAddress);
            int64_t diff = static_cast<int64_t>(chunkAddr) - static_cast<int64_t>(targetAddr);

            if (diff >= -0x70000000LL && diff <= 0x70000000LL) {
                size_t alignedOffset = (chunk.usedBytes + (alignment - 1)) & ~(alignment - 1);
                if (alignedOffset + size <= chunk.totalSize) {
                    void* allocPtr = chunk.baseAddress + alignedOffset;
                    chunk.usedBytes = alignedOffset + size;
                    m_totalAllocated += size;

                    // If Framed MidHook, register per-stub RUNTIME_FUNCTION entry individually
                    if (kind == ChunkKind::MidHookFramed) {
                        RegisterStubUnwindEntry(chunk, static_cast<DWORD>(alignedOffset), static_cast<DWORD>(alignedOffset + size));
                    }

                    DREAM_LOG_DEBUG("DreamPool: Reused existing chunk at 0x%p (kind %d, offset 0x%zx, %zu bytes)",
                                    chunk.baseAddress, static_cast<int>(kind), alignedOffset, size);
                    return allocPtr;
                }
            }
        }

        // 2. Allocate a new 64KB page near target
        constexpr size_t DEFAULT_CHUNK_SIZE = 64 * 1024; // 64 KB
        constexpr size_t UNWIND_HEADER_SIZE = 32;       // Reserve 32 bytes for chunk unwind header in MidHook chunks
        size_t totalNeeded = (kind == ChunkKind::MidHookFramed) ? (size + UNWIND_HEADER_SIZE) : size;
        size_t allocSize = (totalNeeded > DEFAULT_CHUNK_SIZE) ? ((totalNeeded + 0xFFFF) & ~0xFFFF) : DEFAULT_CHUNK_SIZE;

        void* newPage = MemoryHelper::AllocateNearTarget(target, allocSize);
        if (!newPage) {
            DREAM_LOG_ERROR("DreamPool: Failed to allocate near-memory chunk for target 0x%p", target);
            return nullptr;
        }

        TrampolineChunk chunk;
        chunk.baseAddress = reinterpret_cast<uint8_t*>(newPage);
        chunk.totalSize = allocSize;
        chunk.kind = kind;

        void* allocPtr = nullptr;
        if (kind == ChunkKind::MidHookFramed) {
            // Write MinimalUnwindInfo at offset 0
            MinimalUnwindInfo* unwindHeader = reinterpret_cast<MinimalUnwindInfo*>(chunk.baseAddress);
            InitChunkUnwindHeader(unwindHeader);

            size_t startOffset = (UNWIND_HEADER_SIZE + (alignment - 1)) & ~(alignment - 1);
            allocPtr = chunk.baseAddress + startOffset;
            chunk.usedBytes = startOffset + size;

            RegisterStubUnwindEntry(chunk, static_cast<DWORD>(startOffset), static_cast<DWORD>(startOffset + size));
        } else {
            // Pure Leaf chunk (InlineHook): No header, begins directly at offset 0
            allocPtr = chunk.baseAddress;
            chunk.usedBytes = size;
        }

        m_chunks.push_back(std::move(chunk));
        m_totalAllocated += size;

        DREAM_LOG_INFO("DreamPool: Created new near-memory 64KB arena chunk at 0x%p (kind %d, Total chunks: %zu)",
                       newPage, static_cast<int>(kind), m_chunks.size());

        return allocPtr;
    }

    void RegisterStubUnwindEntry(TrampolineChunk& chunk, DWORD beginAddr, DWORD endAddr) {
        auto rf = std::make_unique<RUNTIME_FUNCTION>();
        rf->BeginAddress = beginAddr;
        rf->EndAddress   = endAddr;
        rf->UnwindData   = 0; // Points to MinimalUnwindInfo at offset 0 of chunk

        // Per-Stub registration: Register individually without deleting existing stubs' entries!
        // This eliminates the 1-3 us delete-readd window and completely avoids race conditions.
        if (RtlAddFunctionTable(rf.get(), 1, reinterpret_cast<DWORD64>(chunk.baseAddress))) {
            chunk.registeredFunctions.push_back(std::move(rf));
            DREAM_LOG_DEBUG("DreamPool: Registered per-stub MidHook RUNTIME_FUNCTION at 0x%p [0x%x - 0x%x] (Active stubs: %zu)",
                            chunk.baseAddress, beginAddr, endAddr, chunk.registeredFunctions.size());
        }
    }

    std::mutex m_mutex;
    std::vector<TrampolineChunk> m_chunks;
    size_t m_totalAllocated{0};
};

} // namespace dreamkurumi
