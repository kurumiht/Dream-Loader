// ============================================================================
// DreamCombatSdk.hpp - Enshrouded Holistic Engine Combat & Entity Native SDK
// Auto-generated for dreamkurumiHT Native Plugins & Combat Tooling
// Rule 1 Compliant: Modular Header
// ============================================================================
#pragma once
#include <cstdint>
#include <cstddef>

#pragma pack(push, 1)

// ----------------------------------------------------------------------------
// 1. World Transform & Position (Size: 32 bytes / 0x20)
// Descriptor RVA: 0x18F0530
// ----------------------------------------------------------------------------
struct Keen_ecs_Transform {
    float posX;           // +0x00: World X
    float posY;           // +0x04: World Y
    float posZ;           // +0x08: World Z
    float scale;          // +0x0C: Entity Scale
    float rotX;           // +0x10: Orientation Quaternion X
    float rotY;           // +0x14: Orientation Quaternion Y
    float rotZ;           // +0x18: Orientation Quaternion Z
    float rotW;           // +0x1C: Orientation Quaternion W
};
static_assert(sizeof(Keen_ecs_Transform) == 32, "Size check failed for Keen_ecs_Transform");

// ----------------------------------------------------------------------------
// 2. Network Health & Max Health (Size: 8 bytes / 0x08)
// Client RVA: 0x1A05770 | Server RVA: 0xF91550
// ----------------------------------------------------------------------------
struct Keen_ecs_NetworkHealth {
    float health;         // +0x00: Current Hitpoints
    float healthMax;      // +0x04: Maximum Hitpoints
};
static_assert(sizeof(Keen_ecs_NetworkHealth) == 8, "Size check failed for Keen_ecs_NetworkHealth");

// ----------------------------------------------------------------------------
// 3. 3D Model & Mesh Binding (Size: 4 bytes / 0x04)
// Descriptor RVA: 0x1812370
// ----------------------------------------------------------------------------
struct Keen_ecs_ModelComponent {
    uint32_t modelHandle; // +0x00: 3D Asset / Mesh Resource Handle
};
static_assert(sizeof(Keen_ecs_ModelComponent) == 4, "Size check failed for Keen_ecs_ModelComponent");

// ----------------------------------------------------------------------------
// 4. Skeleton Joint / Bone Node (Size: 40 bytes / 0x28)
// Descriptor RVA: 0x1A6F540
// ----------------------------------------------------------------------------
struct Keen_three_c_Joint3C {
    float orientation[4]; // +0x00: Joint Orientation Quaternion (X, Y, Z, W)
    float position[3];    // +0x10: Local Bone Position (X, Y, Z)
    float scale[3];       // +0x1C: Bone Scale (X, Y, Z)
};
static_assert(sizeof(Keen_three_c_Joint3C) == 40, "Size check failed for Keen_three_c_Joint3C");

// ----------------------------------------------------------------------------
// 5. Target Entity & Selection Info (Size: 136 bytes / 0x88)
// Descriptor RVA: 0x192B590
// ----------------------------------------------------------------------------
struct Keen_ecs_TargetInfo {
    uint8_t  targetTransform[56];  // +0x00: 4x4 World Transformation Matrix
    float    targetingOffset[3];   // +0x38: Aiming Centroid Offset (X, Y, Z)
    uint8_t  _pad0[28];            // +0x44: Internal Engine State
    uint32_t targetRootEntityId;   // +0x60: Root Entity ID
    uint32_t targetEntityId;       // +0x64: Focused Target Entity ID
    uint64_t targetSnappingBoxId;  // +0x68: Snapping Hitbox BoxId
    uint64_t targetImportance;     // +0x70: Aggro / Importance Weight
    uint64_t colliderId;           // +0x78: Hurtbox Collider ID
    uint16_t changeCounter;        // +0x80
    uint8_t  flags[6];             // +0x82
};
static_assert(sizeof(Keen_ecs_TargetInfo) == 136, "Size check failed for Keen_ecs_TargetInfo");

#pragma pack(pop)
