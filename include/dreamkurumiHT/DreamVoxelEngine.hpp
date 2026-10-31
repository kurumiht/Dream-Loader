#pragma once

// ============================================================================
// DreamVoxelEngine.hpp - Proprietary Keen Voxel Engine API Abstraction
//
// Part of the dreamkurumiHT Framework (Item 12: Proprietary Voxel Engine API Abstraction)
// Features:
// - Encapsulates Keen Games' proprietary Holistic Engine Voxel ECS
// - Models VoxelCoord3D, VoxelMaterialType, VoxelBlock, and VoxelChunklet
// - Snapping Control API: Configures keen::ds::VoxelSnappingRule (instant build / precision snap)
// - Voxel Damage & Mining Abstraction: Hooks keen::ecs::MiningNode / TemporaryMining
// - Safe In-Memory Terraforming: Radius excavation and procedural block placement
// ============================================================================

#include <windows.h>
#include <cstdint>
#include <string>
#include <vector>
#include <unordered_map>
#include <mutex>
#include <cmath>
#include "DreamLog.hpp"
#include "DreamCrashGuard.hpp"

namespace dreamkurumi {

#pragma pack(push, 1)
struct VoxelCoord3D {
    int32_t x{0};
    int32_t y{0};
    int32_t z{0};

    bool operator==(const VoxelCoord3D& o) const {
        return x == o.x && y == o.y && z == o.z;
    }
};
#pragma pack(pop)

enum class VoxelMaterialType : uint16_t {
    Air = 0,
    Dirt = 1,
    Stone = 2,
    WoodPlanks = 3,
    RoughWood = 4,
    FlintOre = 10,
    CopperOre = 11,
    IronOre = 12,
    TinOre = 13,
    LapisOre = 14,
    Obsidian = 20,
    ShroudRoot = 30,
    Mycelium = 31,
    Bedrock = 99
};

struct VoxelBlock {
    VoxelCoord3D position;
    VoxelMaterialType material{VoxelMaterialType::Air};
    float health{100.0f};
    uint8_t shapeFlags{0};
    bool isSolid{true};
};

#pragma pack(push, 1)
// Dissected Keen Engine Struct: keen::ds::VoxelSnappingRule
struct KeenVoxelSnappingRule {
    uint8_t pad0[0x50];
    uint8_t maxHorizontalDistance;       // +0x50
    uint8_t maxVerticalDistance;         // +0x51
    uint8_t snapHorizontally;            // +0x52
    uint8_t snapVertically;              // +0x53
    uint8_t pad1[4];
    uint8_t directionalHorizontalSnap;   // +0x58
};

// Dissected Keen Engine Struct: keen::ecs::TemporaryMining
struct KeenTemporaryMining {
    VoxelCoord3D voxelPosition;          // +0x00
    uint8_t pad0[0x0C - sizeof(VoxelCoord3D)];
    float relativeHealth;                // +0x0C (1.0f = full, 0.0f = destroyed)
};
#pragma pack(pop)

class DreamVoxelEngine {
public:
    static DreamVoxelEngine& Instance() {
        static DreamVoxelEngine s_instance;
        return s_instance;
    }

    // ========================================================================
    // Engine Pointer Binding & Dynamic Runtime 'this' Capture (Q1 Senior Dev Feedback)
    // ========================================================================
    void SetEngineGridPointer(void* pGrid) {
        m_engineGrid.store(pGrid);
        m_simulationThreadId.store(GetCurrentThreadId());
        DREAM_LOG_INFO("DreamVoxelEngine: Bound VoxelGrid instance at %p (Thread: %lu)", pGrid, GetCurrentThreadId());
    }

    // Dynamic 'this' capture from MidHook with strict VTable validation
    bool CaptureRuntimeGridThis(void* thisPtr, uintptr_t expectedVTable = 0) {
        if (!thisPtr) return false;

        // If expected VTable is provided, validate *(void**)thisPtr == expectedVTable
        if (expectedVTable != 0) {
            MEMORY_BASIC_INFORMATION mbi{};
            if (!VirtualQuery(thisPtr, &mbi, sizeof(mbi)) ||
                mbi.State != MEM_COMMIT ||
                (mbi.Protect & (PAGE_READONLY | PAGE_READWRITE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE)) == 0) {
                return false;
            }
            uintptr_t actualVTable = *reinterpret_cast<uintptr_t*>(thisPtr);
            if (actualVTable != expectedVTable) {
                DREAM_LOG_WARN("DreamVoxelEngine: Captured 'this' %p failed VTable validation (Expected: 0x%llX, Actual: 0x%llX)",
                               thisPtr, static_cast<unsigned long long>(expectedVTable),
                               static_cast<unsigned long long>(actualVTable));
                return false;
            }
            DREAM_LOG_INFO("DreamVoxelEngine: Successfully captured verified VoxelGrid 'this' at %p (Simulation TID: %lu)",
                           thisPtr, GetCurrentThreadId());
        } else {
            DREAM_LOG_INFO("DreamVoxelEngine: Captured unverified VoxelGrid 'this' at %p (expectedVTable == 0, Simulation TID: %lu)",
                           thisPtr, GetCurrentThreadId());
        }

        m_engineGrid.store(thisPtr);
        m_simulationThreadId.store(GetCurrentThreadId());
        return true;
    }

    // Invalidate pointers when leaving world or returning to main menu
    void InvalidateOnWorldChange() {
        m_engineGrid.store(nullptr);
        m_snappingRule.store(nullptr);
        std::lock_guard<std::mutex> lock(m_mutex);
        m_voxelCache.clear();
        DREAM_LOG_INFO("DreamVoxelEngine: Cleared VoxelGrid pointer and cache on world change");
    }

    void SetSnappingRulePointer(KeenVoxelSnappingRule* pRule) {
        m_snappingRule.store(pRule);
        DREAM_LOG_INFO("DreamVoxelEngine: Bound keen::ds::VoxelSnappingRule at %p", pRule);
    }

    bool HasActiveGrid() const { return m_engineGrid.load() != nullptr; }
    bool HasActiveSnappingRule() const { return m_snappingRule.load() != nullptr; }
    DWORD GetSimulationThreadId() const { return m_simulationThreadId.load(); }

    // ========================================================================
    // Snapping & Construction Control (Item 12 / Section 23 of Internals)
    // ========================================================================
    bool SetSnappingDistance(uint8_t horizontal, uint8_t vertical) {
        auto* rule = m_snappingRule.load();
        if (!rule) return false;

        // Sanity Check: Limit to reasonable game distance range
        if (horizontal > 128) horizontal = 128;
        if (vertical > 128) vertical = 128;

        rule->maxHorizontalDistance = horizontal;
        rule->maxVerticalDistance = vertical;
        DREAM_LOG_DEBUG("DreamVoxelEngine: Updated snapping distance: H=%u, V=%u", horizontal, vertical);
        return true;
    }

    bool SetSnappingEnabled(bool horizontal, bool vertical, bool directional = true) {
        auto* rule = m_snappingRule.load();
        if (!rule) return false;

        rule->snapHorizontally = horizontal ? 1 : 0;
        rule->snapVertically = vertical ? 1 : 0;
        rule->directionalHorizontalSnap = directional ? 1 : 0;
        return true;
    }

    // ========================================================================
    // Voxel Query & Mutation APIs
    // ========================================================================
    VoxelBlock QueryVoxel(int32_t x, int32_t y, int32_t z) {
        std::lock_guard<std::mutex> lock(m_mutex);
        VoxelCoord3D coord{x, y, z};

        // Check local modification cache first
        uint64_t key = MakeCoordKey(x, y, z);
        auto it = m_voxelCache.find(key);
        if (it != m_voxelCache.end()) {
            return it->second;
        }

        VoxelBlock b;
        b.position = coord;
        b.material = VoxelMaterialType::Stone; // Default terrain fallback
        b.health = 100.0f;
        b.isSolid = true;
        return b;
    }

    bool SetVoxel(int32_t x, int32_t y, int32_t z, VoxelMaterialType material, float health = 100.0f) {
        std::lock_guard<std::mutex> lock(m_mutex);
        VoxelBlock b;
        b.position = {x, y, z};
        b.material = material;
        b.health = health;
        b.isSolid = (material != VoxelMaterialType::Air);

        uint64_t key = MakeCoordKey(x, y, z);
        m_voxelCache[key] = b;
        m_totalMutations++;
        return true;
    }

    // Damages voxel at position (e.g. mining / explosion / terraforming)
    bool DamageVoxelAt(int32_t x, int32_t y, int32_t z, float damageAmount) {
        std::lock_guard<std::mutex> lock(m_mutex);
        uint64_t key = MakeCoordKey(x, y, z);
        auto it = m_voxelCache.find(key);
        if (it != m_voxelCache.end()) {
            it->second.health -= damageAmount;
            if (it->second.health <= 0.0f) {
                it->second.material = VoxelMaterialType::Air;
                it->second.isSolid = false;
            }
            m_totalMutations++;
            return true;
        }

        // If not in cache, initialize and apply damage
        VoxelBlock b;
        b.position = {x, y, z};
        b.material = VoxelMaterialType::Stone;
        b.health = 100.0f - damageAmount;
        b.isSolid = (b.health > 0.0f);
        if (!b.isSolid) b.material = VoxelMaterialType::Air;
        m_voxelCache[key] = b;
        m_totalMutations++;
        return true;
    }

    // Excavates or places spherical area of voxels (e.g. instant mine / terraform)
    size_t SetTerrainSphere(float cx, float cy, float cz, float radius, VoxelMaterialType material) {
        constexpr float MAX_TERRAIN_SPHERE_RADIUS = 16.0f; // Limit to safe bounds (~17,000 cells max)
        if (radius > MAX_TERRAIN_SPHERE_RADIUS) {
            DREAM_LOG_WARN("DreamVoxelEngine: Clamping SetTerrainSphere radius from %.1f to %.1f", radius, MAX_TERRAIN_SPHERE_RADIUS);
            radius = MAX_TERRAIN_SPHERE_RADIUS;
        }

        int32_t minX = static_cast<int32_t>(std::floor(cx - radius));
        int32_t maxX = static_cast<int32_t>(std::ceil(cx + radius));
        int32_t minY = static_cast<int32_t>(std::floor(cy - radius));
        int32_t maxY = static_cast<int32_t>(std::ceil(cy + radius));
        int32_t minZ = static_cast<int32_t>(std::floor(cz - radius));
        int32_t maxZ = static_cast<int32_t>(std::ceil(cz + radius));

        float radiusSq = radius * radius;
        size_t count = 0;
        float initialHealth = (material == VoxelMaterialType::Air) ? 0.0f : 100.0f;
        bool isSolid = (material != VoxelMaterialType::Air && initialHealth > 0.0f);

        {
            // Acquire mutex once outside the 3D loop for massive multi-thousand voxel performance gain
            std::lock_guard<std::mutex> lock(m_mutex);
            for (int32_t z = minZ; z <= maxZ; ++z) {
                for (int32_t y = minY; y <= maxY; ++y) {
                    for (int32_t x = minX; x <= maxX; ++x) {
                        float dx = static_cast<float>(x) - cx;
                        float dy = static_cast<float>(y) - cy;
                        float dz = static_cast<float>(z) - cz;
                        if ((dx * dx + dy * dy + dz * dz) <= radiusSq) {
                            uint64_t key = MakeCoordKey(x, y, z);
                            VoxelBlock b;
                            b.position = {x, y, z};
                            b.material = material;
                            b.health = initialHealth;
                            b.isSolid = isSolid;
                            m_voxelCache[key] = b;
                            count++;
                        }
                    }
                }
            }
            m_totalMutations += count;
        }

        DREAM_LOG_INFO("DreamVoxelEngine: Terraform sphere (R=%.1f) modified %zu voxels to material %u",
                       radius, count, static_cast<uint32_t>(material));
        return count;
    }

    size_t GetCachedVoxelCount() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_voxelCache.size();
    }

    size_t GetTotalMutations() const { return m_totalMutations.load(); }

    // ========================================================================
    // Chunk-Boundary Safe Partitioning Formula (8x8x8 Grid Alignment)
    // Ensures spatial bounding boxes snap cleanly to 8-cell chunks (512 cells),
    // preventing boundary mesh tearing and out-of-bounds voxel buffer reads.
    // ========================================================================
    static void AlignToChunkBoundary(int32_t low, int32_t high, int32_t& outAlignedLow, int32_t& outAlignedHigh) {
        int64_t chunkLow = low / 8;
        int64_t chunkHigh = high / 8;
        if (low < 0 && (low % 8 != 0)) --chunkLow;
        if (high > 0 && (high % 8 != 0)) ++chunkHigh;
        outAlignedLow = static_cast<int32_t>(chunkLow * 8);
        outAlignedHigh = static_cast<int32_t>(chunkHigh * 8);
    }

    // ========================================================================
    // High-Level Native Mining & Voxel Modification Dispatcher (Enshrouded 1.0)
    // Invokes the game's native mining_node_events pipeline (0x001A3E30 Server / 0x004013D0 Client)
    // Automatically triggers chunk dirty marking, visual remesh, loot spawning, and network sync.
    // ========================================================================
    typedef void (*FnMiningNodeEvents)(void* pContext, void* pEventBatch);

    void SetNativeMiningHandler(FnMiningNodeEvents pfn) {
        m_pfnMiningNodeEvents.store(pfn);
        DREAM_LOG_INFO("DreamVoxelEngine: Bound native mining_node_events handler at %p", pfn);
    }

    bool HasNativeMiningHandler() const {
        return m_pfnMiningNodeEvents.load() != nullptr;
    }

    bool InvokeNativeMiningEvent(void* pContext, void* pEventBatch) {
        auto pfn = m_pfnMiningNodeEvents.load();
        if (!pfn || !pContext) return false;

        DWORD simTid = m_simulationThreadId.load();
        if (simTid != 0 && GetCurrentThreadId() != simTid) {
            DREAM_LOG_WARN("DreamVoxelEngine: NativeMiningEvent invoked on thread %lu, but simulation thread is %lu!",
                           GetCurrentThreadId(), simTid);
            return false;
        }

        bool ok = false;
        DreamCrashGuard::ExecuteSafely("NativeMiningEvent", [&]() {
            pfn(pContext, pEventBatch);
            ok = true;
        });
        return ok;
    }

private:
    DreamVoxelEngine() = default;

    static uint64_t MakeCoordKey(int32_t x, int32_t y, int32_t z) {
        // Spatial hash combining x, y, z into unique 64-bit key
        uint64_t ux = static_cast<uint64_t>(x) & 0x1FFFFF; // 21 bits
        uint64_t uy = static_cast<uint64_t>(y) & 0x1FFFFF; // 21 bits
        uint64_t uz = static_cast<uint64_t>(z) & 0x3FFFFF; // 22 bits
        return (ux) | (uy << 21) | (uz << 42);
    }

    mutable std::mutex m_mutex;
    std::atomic<void*> m_engineGrid{nullptr};
    std::atomic<KeenVoxelSnappingRule*> m_snappingRule{nullptr};
    std::atomic<DWORD> m_simulationThreadId{0};
    std::atomic<FnMiningNodeEvents> m_pfnMiningNodeEvents{nullptr};
    std::unordered_map<uint64_t, VoxelBlock> m_voxelCache;
    std::atomic<size_t> m_totalMutations{0};
};

} // namespace dreamkurumi
