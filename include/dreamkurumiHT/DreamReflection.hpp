#pragma once
#include <windows.h>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <mutex>
#include "DreamLog.hpp"

namespace dreamkurumi {

// ============================================================================
// DreamReflection: Structural Introspection without RAM Scanning
// ============================================================================
class DreamReflection {
public:
    static DreamReflection& Instance() {
        static DreamReflection s_instance;
        return s_instance;
    }

    void RegisterField(const std::string& structName, const std::string& fieldName, size_t offset) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_offsets[structName + "::" + fieldName] = offset;
    }

    size_t GetFieldOffset(const std::string& structName, const std::string& fieldName, size_t fallback = 0) {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_offsets.find(structName + "::" + fieldName);
        if (it != m_offsets.end()) {
            return it->second;
        }
        return fallback;
    }

    // Direct typed field reference
    template <typename T>
    static T* GetFieldPtr(void* object, size_t offset) {
        if (!object) return nullptr;
        return reinterpret_cast<T*>(reinterpret_cast<uint8_t*>(object) + offset);
    }

    template <typename T>
    T* GetFieldPtr(void* object, const std::string& structName, const std::string& fieldName, size_t fallback = 0) {
        size_t off = GetFieldOffset(structName, fieldName, fallback);
        return GetFieldPtr<T>(object, off);
    }

    template <typename T>
    bool SetFieldValue(void* object, const std::string& structName, const std::string& fieldName, const T& value, size_t fallback = 0) {
        T* ptr = GetFieldPtr<T>(object, structName, fieldName, fallback);
        if (!ptr) return false;
        *ptr = value;
        return true;
    }

    // Preload known Enshrouded / Keen Engine reflection mappings
    void LoadEnshroudedDefaults() {
        // e.g. TriggerShape
        RegisterField("keen::ecs::TriggerShape", "shape", 0x00);
        RegisterField("keen::ecs::TriggerShape", "rangeX", 0x04);
        RegisterField("keen::ecs::TriggerShape", "rangeY", 0x08);
        RegisterField("keen::ecs::TriggerShape", "rangeZ", 0x0C);

        // e.g. IsPlayerInRange
        RegisterField("keen::ecs::IsPlayerInRange", "minRange", 0x00);
        RegisterField("keen::ecs::IsPlayerInRange", "updateDelay", 0x08);

        // GameSettings
        RegisterField("keen::GameSettings", "playerManaFactor", 0x04);
        RegisterField("keen::GameSettings", "playerStaminaFactor", 0x08);
        RegisterField("keen::GameSettings", "enemyDamageFactor", 0x58);
        RegisterField("keen::GameSettings", "enemyPerceptionRangeFactor", 0x64);
        RegisterField("keen::GameSettings", "bossDamageFactor", 0x68);

        DREAM_LOG_INFO("DreamReflection: Loaded default Keen ECS structural field mappings");
    }

private:
    DreamReflection() { LoadEnshroudedDefaults(); }
    std::unordered_map<std::string, size_t> m_offsets;
    std::mutex m_mutex;
};

} // namespace dreamkurumi
