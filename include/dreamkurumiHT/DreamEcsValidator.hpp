#pragma once
#include <cstdint>
#include "DreamCore.hpp"
#include "DreamMemory.hpp"

namespace dreamkurumi {

// ============================================================================
// KeenEntityHandle - 64-bit Generational Entity Handle (keen::ecs::EntityId)
//
// Bits 00..31: 32-bit Table Slot Index
// Bits 32..55: 24-bit Generational Counter (prevents ABA recycling faults)
// Bits 56..63: 08-bit Scope / Entity Type Flags
// ============================================================================
struct KeenEntityHandle {
    union {
        uint64_t raw;
        struct {
            uint32_t index;
            uint32_t generation : 24;
            uint32_t scopeFlags : 8;
        };
    };
};

// ============================================================================
// DreamEcsValidator: Zero-Crash Pre-Validator for Keen Holistic ECS Entities
//
// Validates entity pointer, Lifecycle Byte at [rcx + 0x2D], and Generation ID
// BEFORE submitting into interaction_attempt or immediate_loot batch loops.
//
// Lifecycle Flags at [rcx + 0x2D]:
//   Bit 0 (0x01) = Active / Initialized
//   Bit 1 (0x02) = PendingKill / MarkedForDestruction in current tick
//   Bit 2 (0x04) = Unloaded / StreamingOut
// ============================================================================
class DreamEcsValidator {
public:
    static bool IsEntityValidForInteraction(const void* pEntity, uint64_t expectedEntityId = 0) noexcept {
        if (!pEntity || !MemoryHelper::IsSafeToProbeAddress(pEntity, 0x40)) {
            return false;
        }

        const auto* base = static_cast<const uint8_t*>(pEntity);

        // 1. Check Entity Lifecycle State byte at offset +0x2D
        // Reject if entity is not active (0) or marked PendingKill (0x02) / Unloading (0x04)
        const uint8_t stateFlags = DreamMemory::Read<uint8_t>(base + 0x2D, 0);
        if (stateFlags == 0 || (stateFlags & 0x06) != 0) {
            return false; // Entity is already destroyed or pending teardown in this tick!
        }

        // 2. If caller provides the 64-bit Generational EntityId, verify generation matches
        if (expectedEntityId != 0) {
            const uint64_t liveId = DreamMemory::Read<uint64_t>(base + 0x00, 0);
            if (liveId != 0 && liveId != expectedEntityId) {
                return false; // ABA detected: slot was recycled for a new entity generation!
            }
        }

        return true;
    }
};

inline bool IsEntityValidForInteraction(const void* pEntity, uint64_t expectedEntityId = 0) noexcept {
    return DreamEcsValidator::IsEntityValidForInteraction(pEntity, expectedEntityId);
}

} // namespace dreamkurumi
