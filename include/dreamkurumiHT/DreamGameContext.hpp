#pragma once
#include <windows.h>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>
#include <functional>
#include <mutex>
#include "DreamCore.hpp"
#include "DreamCrashGuard.hpp"
#include "DreamMemory.hpp"
#include "DreamReflection.hpp"
#include "DreamPresets.hpp"
#include "DreamLog.hpp"
#include "DreamEnshroudedTypes.hpp"

namespace dreamkurumi {

// ============================================================================
// 3D Vector representation for in-game positions and coordinates
// ============================================================================
struct DreamVector3 {
    float x{0.0f};
    float y{0.0f};
    float z{0.0f};

    DreamVector3() = default;
    DreamVector3(float _x, float _y, float _z) : x(_x), y(_y), z(_z) {}

    bool operator==(const DreamVector3& o) const {
        return x == o.x && y == o.y && z == o.z;
    }
};

// ============================================================================
// DreamGameContext: Automated Engine Service Locator & Object Registry
// Eliminates the need for mod developers to hunt memory pointers in Cheat Engine!
// ============================================================================
class DreamGameContext {
public:
    static DreamGameContext& Instance() {
        static DreamGameContext s_instance;
        return s_instance;
    }

    // ========================================================================
    // 1. Core Engine Instance Registration & Service Locator
    // ========================================================================

    // Registers or captures the local player entity pointer
    void SetLocalPlayer(void* playerPtr) {
        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        m_localPlayer = playerPtr;
        if (playerPtr) {
            DREAM_LOG_INFO("DreamGameContext: LocalPlayer captured at 0x%p", playerPtr);
            for (const auto& cb : m_onPlayerSpawnCallbacks) {
                DreamCrashGuard::ExecuteSafely("OnPlayerSpawnCallback", [&]() {
                    cb(playerPtr);
                });
            }
        }
    }

    void* GetLocalPlayer() const {
        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        return m_localPlayer;
    }

    bool IsPlayerReady() const {
        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        return m_localPlayer != nullptr && MemoryHelper::IsValidPtr(m_localPlayer);
    }

    // Registers or captures the active World / GameWorld pointer
    void SetWorld(void* worldPtr) {
        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        m_world = worldPtr;
        if (worldPtr) {
            DREAM_LOG_INFO("DreamGameContext: World instance captured at 0x%p", worldPtr);
            for (const auto& cb : m_onWorldLoadedCallbacks) {
                DreamCrashGuard::ExecuteSafely("OnWorldLoadedCallback", [&]() {
                    cb(worldPtr);
                });
            }
        }
    }

    void* GetWorld() const {
        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        return m_world;
    }

    bool IsWorldReady() const {
        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        return m_world != nullptr && MemoryHelper::IsValidPtr(m_world);
    }

    // Registers arbitrary engine subsystems by name (e.g. "InventoryManager", "EntityManager")
    void RegisterService(const std::string& serviceName, void* servicePtr) {
        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        m_services[serviceName] = servicePtr;
        DREAM_LOG_INFO("DreamGameContext: Registered service '%s' at 0x%p", serviceName.c_str(), servicePtr);
    }

    void* GetService(const std::string& serviceName) const {
        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        auto it = m_services.find(serviceName);
        return (it != m_services.end()) ? it->second : nullptr;
    }

    bool HasService(const std::string& serviceName) const {
        return GetService(serviceName) != nullptr;
    }

    // ========================================================================
    // 2. Lifecycle Event Subscriptions
    // ========================================================================

    // Hook called when the local player is spawned or entered the world
    void OnPlayerSpawn(std::function<void(void*)> callback) {
        if (!callback) return;
        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        m_onPlayerSpawnCallbacks.push_back(callback);
        if (m_localPlayer && MemoryHelper::IsValidPtr(m_localPlayer)) {
            DreamCrashGuard::ExecuteSafely("OnPlayerSpawnImmediate", [&]() {
                callback(m_localPlayer);
            });
        }
    }

    // Hook called when the game world is loaded
    void OnWorldLoaded(std::function<void(void*)> callback) {
        if (!callback) return;
        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        m_onWorldLoadedCallbacks.push_back(callback);
        if (m_world && MemoryHelper::IsValidPtr(m_world)) {
            DreamCrashGuard::ExecuteSafely("OnWorldLoadedImmediate", [&]() {
                callback(m_world);
            });
        }
    }

    // ========================================================================
    // 3. High-Level Player Actions & Coordinates
    // ========================================================================

    // Reads player world coordinates (configurable position offset or default +0x40)
    DreamVector3 GetPlayerPosition(uintptr_t posOffset = 0x40) const {
        void* p = GetLocalPlayer();
        if (!p || !MemoryHelper::IsValidPtr(p)) return DreamVector3{};
        return DreamMemory::Read<DreamVector3>(reinterpret_cast<const uint8_t*>(p) + posOffset, DreamVector3{});
    }

    // Teleports player to new coordinate
    bool TeleportPlayer(const DreamVector3& targetPos, uintptr_t posOffset = 0x40) {
        void* p = GetLocalPlayer();
        if (!p || !MemoryHelper::IsValidPtr(p)) return false;
        bool ok = DreamMemory::Write<DreamVector3>(reinterpret_cast<uint8_t*>(p) + posOffset, targetPos);
        if (ok) {
            DREAM_LOG_INFO("DreamGameContext: Teleported player to (%.2f, %.2f, %.2f)", targetPos.x, targetPos.y, targetPos.z);
        }
        return ok;
    }

    // Teleport player by (x, y, z)
    bool TeleportPlayer(float x, float y, float z, uintptr_t posOffset = 0x40) {
        return TeleportPlayer(DreamVector3(x, y, z), posOffset);
    }

    // ========================================================================
    // 3D World Cursor & Crosshair Coordinates (Keen world_cursor reflection)
    // ========================================================================
    void SetWorldCursor(const DreamWorldCursor& cursor) {
        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        m_cursor = cursor;
    }

    DreamWorldCursor GetWorldCursor() const {
        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        return m_cursor;
    }

    bool GetWorldCursorPosition(float* outX, float* outY, float* outZ) const {
        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        if (!m_cursor.isValid) return false;
        if (outX) *outX = m_cursor.posX;
        if (outY) *outY = m_cursor.posY;
        if (outZ) *outZ = m_cursor.posZ;
        return true;
    }

    bool GetWorldCursorRay(float* outOriginX, float* outOriginY, float* outOriginZ,
                          float* outDirX, float* outDirY, float* outDirZ) const {
        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        if (!m_cursor.isValid) return false;
        if (outOriginX) *outOriginX = m_cursor.originX;
        if (outOriginY) *outOriginY = m_cursor.originY;
        if (outOriginZ) *outOriginZ = m_cursor.originZ;
        if (outDirX) *outDirX = m_cursor.dirX;
        if (outDirY) *outDirY = m_cursor.dirY;
        if (outDirZ) *outDirZ = m_cursor.dirZ;
        return true;
    }

    // ========================================================================
    // 4. Quick Gameplay Modification Presets (Directly integrated with Keen engine)
    // ========================================================================

    // Set Live GodMode (Infinite Health & invulnerability)
    void SetGodMode(bool enable) {
        auto& presets = DreamPresets::Instance();
        presets.SetGodMode(enable);
        DREAM_LOG_INFO("DreamGameContext: GodMode set to %s", enable ? "ENABLED" : "DISABLED");
    }

    // Set Live GhostMode (Invisibility & Enemy pacification)
    void SetGhostMode(bool enable) {
        auto& presets = DreamPresets::Instance();
        presets.SetGhostMode(enable);
        DREAM_LOG_INFO("DreamGameContext: GhostMode set to %s", enable ? "ENABLED" : "DISABLED");
    }

    // Set Infinite Stamina
    void SetInfiniteStamina(bool enable) {
        auto& presets = DreamPresets::Instance();
        presets.SetInfiniteStamina(enable);
        DREAM_LOG_INFO("DreamGameContext: InfiniteStamina set to %s", enable ? "ENABLED" : "DISABLED");
    }

    // Set Infinite Mana
    void SetInfiniteMana(bool enable) {
        auto& presets = DreamPresets::Instance();
        presets.SetInfiniteMana(enable);
        DREAM_LOG_INFO("DreamGameContext: InfiniteMana set to %s", enable ? "ENABLED" : "DISABLED");
    }

    // Set Unlimited Glider Flight
    void SetUnlimitedFlight(bool enable) {
        auto& presets = DreamPresets::Instance();
        presets.SetUnlimitedFlight(enable);
        DREAM_LOG_INFO("DreamGameContext: UnlimitedFlight set to %s", enable ? "ENABLED" : "DISABLED");
    }

    // ========================================================================
    // 5. Reflection-Assisted Member Property Access (Zero Pointer Hunting!)
    // ========================================================================

    template <typename T>
    T GetPlayerField(const std::string& structName, const std::string& fieldName, T fallback = T{}) {
        void* p = GetLocalPlayer();
        if (!p) return fallback;
        T* pField = DreamReflection::Instance().GetFieldPtr<T>(p, structName, fieldName);
        return pField ? *pField : fallback;
    }

    template <typename T>
    bool SetPlayerField(const std::string& structName, const std::string& fieldName, const T& value) {
        void* p = GetLocalPlayer();
        if (!p) return false;
        return DreamReflection::Instance().SetFieldValue<T>(p, structName, fieldName, value);
    }

private:
    DreamGameContext() = default;
    mutable std::recursive_mutex m_mutex;
    void* m_localPlayer{nullptr};
    void* m_world{nullptr};
    std::unordered_map<std::string, void*> m_services;
    std::vector<std::function<void(void*)>> m_onPlayerSpawnCallbacks;
    std::vector<std::function<void(void*)>> m_onWorldLoadedCallbacks;
    DreamWorldCursor m_cursor{};
};

// Top-level facade alias for clean developer syntax: DreamGame::GetLocalPlayer()
using DreamGame = DreamGameContext;

} // namespace dreamkurumi
