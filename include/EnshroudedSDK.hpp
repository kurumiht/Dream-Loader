#pragma once
#include <cstdint>
#include <cmath>
#include <string>
#include <vector>

namespace dreamkurumi::sdk {

// ============================================================================
// Enshrouded 1.0 Known Component Types (Holistic Engine)
// ============================================================================
enum class ComponentType : uint16_t {
    Unknown                 = 0,
    Transform               = 10001,
    Physics                 = 10050,
    Inventory               = 10150,
    Interaction             = 10165,
    Health                  = 10190,  // ds Health (Server)
    Health_Client           = 3038,   // ecs Health (Client)
    NetworkPlayerAttributes = 10201,  // ds Stamina & Attributes (Server)
    PlayerAttributes_Client = 3049,   // ecs Stamina & Attributes (Client)
    LocomotionStamina       = 9645,   // Locomotion state stamina
    CombatState             = 10250,
    Perception              = 10300,
    ShroudState             = 10280
};

// ============================================================================
// Math & Vector Structures
// ============================================================================
#pragma pack(push, 1)
struct Vector3 {
    float x{0.0f};
    float y{0.0f};
    float z{0.0f};

    Vector3() = default;
    Vector3(float _x, float _y, float _z) : x(_x), y(_y), z(_z) {}

    float Length() const {
        return std::sqrt(x * x + y * y + z * z);
    }

    float Distance(const Vector3& o) const {
        float dx = x - o.x;
        float dy = y - o.y;
        float dz = z - o.z;
        return std::sqrt(dx * dx + dy * dy + dz * dz);
    }

    Vector3 operator+(const Vector3& o) const { return Vector3(x + o.x, y + o.y, z + o.z); }
    Vector3 operator-(const Vector3& o) const { return Vector3(x - o.x, y - o.y, z - o.z); }
    Vector3 operator*(float s) const { return Vector3(x * s, y * s, z * s); }
};

struct Vector4 {
    float x{0.0f};
    float y{0.0f};
    float z{0.0f};
    float w{1.0f};
};

// ============================================================================
// Health & Vitality Components
// ============================================================================
struct HealthComponent {
    uint32_t currentHealth; // +0x00
    uint32_t maxHealth;     // +0x04
};

// ============================================================================
// Stamina & Player Attribute Components
// ============================================================================
struct PlayerAttributesComponent {
    char pad_00[0x8C];
    uint16_t currentStamina; // +0x8C
    uint16_t maxStamina;     // +0x8E
};

// ============================================================================
// Combat Damage Layout (Enshrouded 1.0)
// ============================================================================
struct DamageSetup {
    float physicalCutDamage;        // +0x00
    float physicalPierceDamage;     // +0x04
    float physicalBluntDamage;      // +0x08
    float magicalFireDamage;        // +0x0C
    float magicalIceDamage;         // +0x10
    float magicalFogDamage;         // +0x14
    float magicalLightningDamage;   // +0x18
    float healing;                  // +0x1C
    float poisonDamage;             // +0x20
    int32_t woodDamage;             // +0x24
    int32_t stoneDamage;            // +0x28
    int32_t metalDamage;            // +0x2C
    int32_t explosionDamage;        // +0x30

    float GetTotalPhysical() const {
        return physicalCutDamage + physicalPierceDamage + physicalBluntDamage;
    }

    float GetTotalMagical() const {
        return magicalFireDamage + magicalIceDamage + magicalFogDamage + magicalLightningDamage;
    }

    float GetTotalDamage() const {
        return GetTotalPhysical() + GetTotalMagical() + poisonDamage + static_cast<float>(explosionDamage);
    }

    void ScaleDamage(float multiplier) {
        physicalCutDamage *= multiplier;
        physicalPierceDamage *= multiplier;
        physicalBluntDamage *= multiplier;
        magicalFireDamage *= multiplier;
        magicalIceDamage *= multiplier;
        magicalFogDamage *= multiplier;
        magicalLightningDamage *= multiplier;
        poisonDamage *= multiplier;
        explosionDamage = static_cast<int32_t>(explosionDamage * multiplier);
    }

    void Nullify() {
        physicalCutDamage = 0.0f;
        physicalPierceDamage = 0.0f;
        physicalBluntDamage = 0.0f;
        magicalFireDamage = 0.0f;
        magicalIceDamage = 0.0f;
        magicalFogDamage = 0.0f;
        magicalLightningDamage = 0.0f;
        poisonDamage = 0.0f;
        explosionDamage = 0;
    }
};

// ============================================================================
// Interaction & Event Batch Layout (Enshrouded 1.0)
// ============================================================================
struct EventBatch {
    char pad[0x18];           // 0x00 - 0x17 (24 bytes)
    uint32_t count;           // 0x18 - 0x1B (4 bytes element count)
    uint32_t reserved;        // 0x1C - 0x1F (4 bytes alignment)
    void* pBuffer;            // 0x20 - 0x27 (Pointer to item array)
    uint64_t capacity;        // 0x28 - 0x2F (8 bytes capacity)
};

// ============================================================================
// Fishing Minigame State Layout
// ============================================================================
struct FishingState {
    char pad_00[0x50];
    uint8_t fightsToPlay;     // +0x50
    uint8_t pad_51;
    uint8_t enableMinigame;   // +0x52
    char pad_53[0x09];
    float maxFishStamina;     // +0x5C
    float currentFishStamina; // +0x60
    char pad_64[0x04];
    float maxLineTension;     // +0x68
    float currentLineTension; // +0x6C
};

#pragma pack(pop)

// ============================================================================
// Helper Methods for Entity Component Access
// ============================================================================
inline bool IsValidPointer(const void* ptr, size_t minSize = 8) {
    if (!ptr) return false;
    uintptr_t addr = reinterpret_cast<uintptr_t>(ptr);
    return (addr >= 0x10000 && addr <= 0x00007FFFFFF00000ULL);
}

} // namespace dreamkurumi::sdk
