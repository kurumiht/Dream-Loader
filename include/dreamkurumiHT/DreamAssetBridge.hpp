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
