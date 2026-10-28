#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdint>
#include <cstddef>
#include <cmath>

// ============================================================================
// dreamkurumiHT Enshrouded Verified Engine Types & Components
// Compile-time offset assertions and safe bitmask inspection helpers
// ============================================================================

namespace dreamkurumi::enshrouded {

#pragma pack(push, 1)

// Random loot event (24 bytes) - chests, crates, interactive items
struct RandomInteractionEvent {
    uint8_t pad0[8];        // 0x00 - 0x07
    uint32_t playerId;      // 0x08 - 0x0B
    uint32_t entityId;      // 0x0C - 0x0F
    uint8_t pad1[8];        // 0x10 - 0x17
};
static_assert(sizeof(RandomInteractionEvent) == 24, "RandomInteractionEvent size must be 24 bytes");
static_assert(offsetof(RandomInteractionEvent, playerId) == 0x08, "playerId must be at 0x08");
static_assert(offsetof(RandomInteractionEvent, entityId) == 0x0C, "entityId must be at 0x0C");

// Immediate loot event (120 bytes) - crops, plants, direct harvesting
struct ImmediateInteractionEvent {
    uint8_t pad0[8];        // 0x00 - 0x07
    uint32_t playerId;      // 0x08 - 0x0B (guestId)
    uint32_t entityId;      // 0x0C - 0x0F
    uint32_t offerId;       // 0x10 - 0x13
    uint8_t data[100];      // 0x14 - 0x77
};
static_assert(sizeof(ImmediateInteractionEvent) == 120, "ImmediateInteractionEvent size must be 120 bytes");
static_assert(offsetof(ImmediateInteractionEvent, playerId) == 0x08, "playerId must be at 0x08");
static_assert(offsetof(ImmediateInteractionEvent, entityId) == 0x0C, "entityId must be at 0x0C");
static_assert(offsetof(ImmediateInteractionEvent, offerId) == 0x10, "offerId must be at 0x10");

// EventBatch - Core engine event container for Hook 12A/B and Hook 13A/B
struct EventBatch {
    char pad[0x18];           // 0x00 - 0x17 (24 bytes)
    void* events;             // 0x18 - 0x1F (pointer to array of events)
    char pad2[0x08];          // 0x20 - 0x27 (8 bytes)
    uint32_t count;           // 0x28 - 0x2B (number of active events)
    uint32_t capacity;        // 0x2C - 0x2F (allocated capacity)
};
static_assert(sizeof(EventBatch) == 0x30, "EventBatch size must be 48 bytes (0x30)");
static_assert(offsetof(EventBatch, events) == 0x18, "EventBatch::events must be at 0x18");
static_assert(offsetof(EventBatch, count) == 0x28, "EventBatch::count must be at 0x28");
static_assert(offsetof(EventBatch, capacity) == 0x2C, "EventBatch::capacity must be at 0x2C");

// Fixed-Point Coordinate Conversion Constant (1 meter = 2^32 fixed point units)
inline constexpr double FIXED_POINT_SCALE = 4294967296.0;

inline double FixedToMeters(int64_t fixedVal) {
    return static_cast<double>(fixedVal) / FIXED_POINT_SCALE;
}

inline int64_t MetersToFixed(double meters) {
    return static_cast<int64_t>(meters * FIXED_POINT_SCALE);
}

// Component IDs
namespace ComponentIds {
    inline constexpr uint16_t SpatialTransform            = 0x7D;   // 125 (Fixed-point X/Y/Z)
    inline constexpr uint16_t InteractionOffer            = 0x105;  // 261 (Offer acceptance)
    inline constexpr uint16_t PickupItem                  = 0x146;  // 326 (Pickup property)
    inline constexpr uint16_t DestroyOnLoot               = 0x147;  // 327
    inline constexpr uint16_t AreaLootOverride            = 0x148;  // 328 (1.0+ Vacuum loot candidate)
    inline constexpr uint16_t DisableColliderModeChanges  = 0x149;  // 329 (1.0+ Loot-all target marker)
    inline constexpr uint16_t PickupItemZone              = 0x14A;  // 330
    inline constexpr uint16_t TriggerShape                = 0x14B;  // 331
    inline constexpr uint16_t IsPlayerInRange             = 0x14C;  // 332
    inline constexpr uint16_t Inventory                   = 0x174;  // 372
    inline constexpr uint16_t RandomLoot                  = 0x175;  // 373 (Chests, world containers)
    inline constexpr uint16_t DefaultInventory            = 0x17E;  // 382
    inline constexpr uint16_t ResourceNodePickupDrops     = 0x17F;  // 383 (Trees, ores, breakables)
}

// Archetype Bitmask Filters
namespace ArchetypeBits {
    // Word 4 (+0x20 in Archetype)
    inline constexpr uint64_t HasInteractionOffer           = (1ULL << 5);   // Component 0x105
    inline constexpr uint64_t HasInventory                  = (1ULL << 52);  // Component 0x174
    inline constexpr uint64_t HasDefaultInventory           = (1ULL << 62);  // Component 0x17E

    // Word 5 (+0x28 in Archetype)
    inline constexpr uint64_t DestroyOnLootMask             = (0x7ULL << 9); // Bits 9, 10, 11 (Component 0x147)
    inline constexpr uint64_t HasAreaLootOverride           = (1ULL << 12);  // Component 0x148
    inline constexpr uint64_t HasDisableColliderModeChanges = (1ULL << 13);  // Component 0x149
}

// Fast In-Memory Classification Helpers
inline bool IsAreaLootCandidateArchetype(const void* pArch) {
    if (!pArch) return false;
    const uint64_t* compWords = reinterpret_cast<const uint64_t*>(reinterpret_cast<const char*>(pArch) + 0x20);
    uint64_t word5 = compWords[1]; // +0x28
    return (word5 & (ArchetypeBits::HasAreaLootOverride | ArchetypeBits::HasDisableColliderModeChanges)) != 0;
}

// Fast In-Memory Classification Helpers
inline bool IsGroundLootArchetype(const void* pArch) {
    if (!pArch) return false;
    const uint64_t* compWords = reinterpret_cast<const uint64_t*>(reinterpret_cast<const char*>(pArch) + 0x20);

    // Check Word 5 for DestroyOnLoot
    uint64_t word5 = compWords[1]; // +0x28
    if ((word5 & ArchetypeBits::DestroyOnLootMask) != 0) return true;

    // Check Word 4 for Inventories
    uint64_t word4 = compWords[0]; // +0x20
    if ((word4 & (ArchetypeBits::HasInventory | ArchetypeBits::HasDefaultInventory)) != 0) return true;

    return false;
}

inline bool IsInteractableArchetype(const void* pArch) {
    if (!pArch) return false;
    uint64_t word4 = *reinterpret_cast<const uint64_t*>(reinterpret_cast<const char*>(pArch) + 0x20);
    return (word4 & ArchetypeBits::HasInteractionOffer) != 0;
}

#pragma pack(pop)

// ============================================================================
// EcsResolver - Dynamic Enshrouded Entity & Component Resolution Engine
// ============================================================================
class EcsResolver {
public:
    static EcsResolver& Instance() {
        static EcsResolver s_inst;
        return s_inst;
    }

    bool Initialize() {
        if (m_initialized) return true;
        uintptr_t base = reinterpret_cast<uintptr_t>(GetModuleHandleA(NULL));
        if (!base) return false;

        // Default RVAs: GetEntity = 0x5C8850, GetComponent = 0x5D4CC0
        m_pfnGetEntity = reinterpret_cast<FnGetEntity>(base + 0x5C8850);
        m_pfnGetComp   = reinterpret_cast<FnGetComp>(base + 0x5D4CC0);
        m_initialized = (m_pfnGetEntity != nullptr && m_pfnGetComp != nullptr);
        return m_initialized;
    }

    void* GetEntity(void* pContext, uint32_t entityId) {
        if (!m_initialized) Initialize();
        if (!m_pfnGetEntity || !pContext || entityId == 0) return nullptr;
        return m_pfnGetEntity(pContext, entityId);
    }

    void* GetComponent(void* pEntity, uint16_t compId) {
        if (!m_initialized) Initialize();
        if (!m_pfnGetComp || !pEntity) return nullptr;
        void* outTuple[2] = { nullptr, nullptr };
        m_pfnGetComp(outTuple, pEntity, &compId);
        return outTuple[0];
    }

    bool GetEntityPosition(void* pEntity, float* outX, float* outY, float* outZ) {
        void* pSpatial = GetComponent(pEntity, ComponentIds::SpatialTransform);
        if (!pSpatial) return false;

        int64_t rawX = *reinterpret_cast<const int64_t*>(pSpatial);
        int64_t rawY = *reinterpret_cast<const int64_t*>(reinterpret_cast<const char*>(pSpatial) + 0x08);
        int64_t rawZ = *reinterpret_cast<const int64_t*>(reinterpret_cast<const char*>(pSpatial) + 0x10);

        if (outX) *outX = static_cast<float>(FixedToMeters(rawX));
        if (outY) *outY = static_cast<float>(FixedToMeters(rawY));
        if (outZ) *outZ = static_cast<float>(FixedToMeters(rawZ));
        return true;
    }

private:
    using FnGetEntity = void* (*)(void* pContext, uint32_t entityId);
    using FnGetComp   = void* (*)(void* outTuple[2], void* pEntity, const uint16_t* pCompId);

    FnGetEntity m_pfnGetEntity{nullptr};
    FnGetComp   m_pfnGetComp{nullptr};
    bool m_initialized{false};
};

} // namespace dreamkurumi::enshrouded
