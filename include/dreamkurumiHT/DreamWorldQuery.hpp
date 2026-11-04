#pragma once
#include <cstdint>
#include <cstddef>
#include "DreamCore.hpp"
#include "DreamCrashGuard.hpp"
#include "DreamEnshroudedTypes.hpp"
#include "DreamGameContext.hpp"

// ============================================================================
// DreamWorldQuery.hpp - Modular Enshrouded ECS Spatial Query Service
// 
// Decoupled from Core Harmony Engine (Rule 1 Compliant)
// Implements Dev 1's Snapshot Buffer Pattern & 5-Layer Concurrency Defense:
// 1. Output Array (No mod callbacks during ECS iteration, eliminates re-entrancy)
// 2. Bound-checked capacity & table pointer probes
// 3. Lifecycle state byte [pEnt + 0x2D] skip destroyed / PendingKill entities
// 4. Archetype bitmask pre-filtering
// 5. Post-snapshot handle validation (IsEntityAlive)
// ============================================================================

namespace dreamkurumi::enshrouded {

struct EntitySnapshot {
    uint32_t entityId{0};
    float position[3]{0.0f, 0.0f, 0.0f};
    uint32_t archetypeMask{0};
};

struct SpatialQueryResult {
    size_t count{0};
    bool truncated{false};
};

class DreamWorldQuery {
public:
    static DreamWorldQuery& Instance() {
        static DreamWorldQuery s_inst;
        return s_inst;
    }

    // Safe ECS Spatial Query with Output Array (Snapshot Buffer Pattern)
    // Enforces 5-Layer Concurrency Defense without invoking mod callbacks during ECS iteration
    SpatialQueryResult QueryInRadiusSnapshot(
        void* pContext,
        const float center[3],
        float radius,
        uint32_t archetypeFilter,
        EntitySnapshot* outBuffer,
        size_t maxCount)
    {
        SpatialQueryResult result{};
        if (!center || radius <= 0.0f || !outBuffer || maxCount == 0) return result;

        void* pWorld = nullptr;
        if (pContext) {
            size_t cap = 0;
            if (MemoryHelper::SafeRead(reinterpret_cast<const char*>(pContext) + 0x158, cap) && cap >= 4 && cap <= 10000000) {
                pWorld = pContext;
            } else {
                void* pObj0 = nullptr;
                if (MemoryHelper::SafeRead(pContext, pObj0) && pObj0) {
                    MemoryHelper::SafeRead(reinterpret_cast<const char*>(pObj0) + 0x30, pWorld);
                }
            }
        }
        if (!pWorld) {
            pWorld = DreamGameContext::Instance().GetWorld();
        }
        if (!pWorld || !MemoryHelper::IsValidPtr(pWorld)) return result;

        DreamCrashGuard::ExecuteSafely("SpatialQuerySnapshot", [&]() {
            size_t capacity = 0;
            uint32_t* pKeys = nullptr;
            uint64_t* pBitmap = nullptr;
            if (!MemoryHelper::SafeRead(reinterpret_cast<const char*>(pWorld) + 0x158, capacity) || capacity == 0 || capacity > 10000000) return;
            if (!MemoryHelper::SafeRead(reinterpret_cast<const char*>(pWorld) + 0x150, pBitmap) || !pBitmap) return;
            if (!MemoryHelper::SafeRead(reinterpret_cast<const char*>(pWorld) + 0x170, pKeys) || !pKeys) return;

            const float radiusSq = radius * radius;
            const size_t numWords = (capacity + 63) >> 6;

            for (size_t w = 0; w < numWords; ++w) {
                uint64_t mask = 0;
                if (!MemoryHelper::SafeRead(&pBitmap[w], mask) || mask == 0) continue;

                for (int b = 0; b < 64; ++b) {
                    if (!(mask & (1ULL << b))) continue;
                    size_t idx = (w << 6) | b;
                    if (idx >= capacity) break;

                    uint32_t entId = 0;
                    if (!MemoryHelper::SafeRead(&pKeys[idx], entId) || entId == 0) continue;

                    void* pEnt = EcsResolver::Instance().GetEntity(pContext ? pContext : pWorld, entId);
                    if (!pEnt || !MemoryHelper::IsValidPtr(pEnt)) continue;

                    // Layer 3: Lifecycle state byte check [rcx + 0x2D]
                    uint8_t stateByte = 0;
                    if (!MemoryHelper::SafeRead(reinterpret_cast<const char*>(pEnt) + 0x2D, stateByte) ||
                        stateByte == 0 || (stateByte & 0x06) != 0) {
                        continue;
                    }

                    // Layer 4: Archetype check
                    void* pArch = nullptr;
                    if (!MemoryHelper::SafeRead(reinterpret_cast<const char*>(pEnt) + 0x18, pArch) || !pArch) continue;
                    uint32_t archWord = 0;
                    MemoryHelper::SafeRead(reinterpret_cast<const char*>(pArch) + 0x28, archWord);
                    if (archetypeFilter != 0 && (archWord & archetypeFilter) == 0) continue;

                    // Layer 5: Distance check
                    float pos[3] = { 0.0f, 0.0f, 0.0f };
                    if (!EcsResolver::Instance().GetEntityPosition(pEnt, &pos[0], &pos[1], &pos[2])) continue;

                    float dx = pos[0] - center[0];
                    float dy = pos[1] - center[1];
                    float dz = pos[2] - center[2];
                    if ((dx * dx + dy * dy + dz * dz) > radiusSq) continue;

                    if (result.count >= maxCount) {
                        result.truncated = true;
                        return;
                    }

                    outBuffer[result.count].entityId = entId;
                    outBuffer[result.count].position[0] = pos[0];
                    outBuffer[result.count].position[1] = pos[1];
                    outBuffer[result.count].position[2] = pos[2];
                    outBuffer[result.count].archetypeMask = archWord;
                    result.count++;
                }
            }
        });

        return result;
    }

    // Entity validation helper to verify entity is still alive after snapshot
    static bool IsEntityAlive(void* pContext, uint32_t entityId) {
        if (!pContext || entityId == 0) return false;
        void* pEnt = EcsResolver::Instance().GetEntity(pContext, entityId);
        if (!pEnt || !MemoryHelper::IsValidPtr(pEnt)) return false;
        uint8_t stateByte = 0;
        if (!MemoryHelper::SafeRead(reinterpret_cast<const char*>(pEnt) + 0x2D, stateByte) ||
            stateByte == 0 || (stateByte & 0x06) != 0) {
            return false;
        }
        return true;
    }
};

} // namespace dreamkurumi::enshrouded
