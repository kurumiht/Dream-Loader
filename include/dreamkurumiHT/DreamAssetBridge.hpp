#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <string>
#include <vector>
#include <unordered_map>
#include <mutex>
#include <cstdint>
#include <cstring>
#include "DreamLog.hpp"
#include "DreamCrashGuard.hpp"

namespace dreamkurumi {

// ============================================================================
// DreamAssetBridge - Dynamic Engine Asset & Resource Interception Engine
// Enables runtime swapping of Fonts, LocaTags, Textures & Templates (0% PAK edit)
// ============================================================================
#pragma pack(push, 1)
// Dissected Keen Games Holistic Engine KFC4 Container Header
struct Kfc4ContainerHeader {
    uint32_t magic;      // 0x00: 'K','F','C','4' (0x3443464B in Little-Endian)
    uint32_t version;    // 0x04: Format Version (4 for Enshrouded 1.0)
    uint32_t flags;      // 0x08: Compression & encryption flags
    uint32_t entryCount; // 0x0C: Total resource entries
};
#pragma pack(pop)
static_assert(sizeof(Kfc4ContainerHeader) == 16, "Kfc4ContainerHeader must be 16 bytes");

class DreamAssetBridge {
public:
    struct AssetData {
        std::vector<uint8_t> buffer;
        std::string originMod;
    };

    static DreamAssetBridge& Instance() {
        static DreamAssetBridge s_instance;
        return s_instance;
    }

    // Register an in-memory asset replacement buffer
    bool RegisterAssetOverride(const std::string& resourceType,
                               const std::string& resourceIdentifier,
                               const void* data,
                               size_t size,
                               const std::string& originMod = "generic") {
        if (!data || size == 0) return false;

        std::string key = MakeKey(resourceType, resourceIdentifier);
        std::lock_guard<std::mutex> lock(m_mutex);

        AssetData asset;
        asset.originMod = originMod;
        asset.buffer.assign(reinterpret_cast<const uint8_t*>(data), reinterpret_cast<const uint8_t*>(data) + size);

        m_assetOverrides[key] = std::move(asset);
        DREAM_LOG_INFO("DreamAssetBridge: Registered asset override for '%s' (%zu bytes) from mod '%s'",
                       key.c_str(), size, originMod.c_str());
        return true;
    }

    // Query if an asset override is registered
    bool GetAssetOverride(const std::string& resourceType,
                          const std::string& resourceIdentifier,
                          const void** outData,
                          size_t* outSize) {
        if (!outData || !outSize) return false;

        std::string key = MakeKey(resourceType, resourceIdentifier);
        std::lock_guard<std::mutex> lock(m_mutex);

        auto it = m_assetOverrides.find(key);
        if (it != m_assetOverrides.end()) {
            *outData = it->second.buffer.data();
            *outSize = it->second.buffer.size();
            return true;
        }
        return false;
    }

    // Register a dynamic localization text tag (LocaTag) replacement
    bool RegisterLocaTag(const std::string& tag, const std::string& utf8Text) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_locaTags[tag] = utf8Text;
        DREAM_LOG_DEBUG("DreamAssetBridge: Registered LocaTag override: '%s' -> '%s'",
                        tag.c_str(), utf8Text.c_str());
        return true;
    }

    // Query localization text tag replacement
    bool GetLocaTag(const std::string& tag, std::string& outText) {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_locaTags.find(tag);
        if (it != m_locaTags.end()) {
            outText = it->second;
            return true;
        }
        return false;
    }

    // Clear all registered asset overrides and localization tags
    void ClearAll() {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_assetOverrides.clear();
        m_locaTags.clear();
        DREAM_LOG_INFO("DreamAssetBridge: All dynamic asset overrides cleared cleanly");
    }

// ========================================================================
    // KFC4 Container Header Validation (Keen Games Engine Format)
    // ========================================================================
    static bool IsValidKfc4Container(const void* data, size_t size) {
        if (!data || size < sizeof(Kfc4ContainerHeader)) return false;
        const auto* hdr = reinterpret_cast<const Kfc4ContainerHeader*>(data);
        return hdr->magic == 0x3443464B; // "KFC4"
    }

    static uint32_t GetKfc4Version(const void* data, size_t size) {
        if (!IsValidKfc4Container(data, size)) return 0;
        return reinterpret_cast<const Kfc4ContainerHeader*>(data)->version;
    }

    size_t GetAssetOverrideCount() const {
        return m_assetOverrides.size();
    }

    size_t GetLocaTagCount() const {
        return m_locaTags.size();
    }

private:
    DreamAssetBridge() = default;

    static std::string MakeKey(const std::string& type, const std::string& id) {
        return type + "::" + id;
    }

    mutable std::mutex m_mutex;
    std::unordered_map<std::string, AssetData> m_assetOverrides;
    std::unordered_map<std::string, std::string> m_locaTags;
};

} // namespace dreamkurumi
