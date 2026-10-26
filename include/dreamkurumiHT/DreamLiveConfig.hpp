#pragma once
#include <windows.h>
#include <string>
#include <unordered_map>
#include <functional>
#include <mutex>
#include <vector>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include "DreamToml.hpp"
#include "DreamBackup.hpp"

namespace dreamkurumi {

enum class ConfigType {
    Bool,
    Int,
    Float,
    String
};

struct ConfigProperty {
    std::string key;
    ConfigType type;
    std::string valueStr;
    std::string description;
    std::vector<std::function<void(const std::string&)>> onChangeCallbacks;
};

// Forward declaration
class DreamLiveConfig;

// ============================================================================
// ConfigEntry<T>: Strongly-typed configuration property binding (BepInEx style)
// ============================================================================
template <typename T>
class ConfigEntry {
public:
    ConfigEntry() = default;
    ConfigEntry(const std::string& section, const std::string& key, const T& defaultValue,
                const std::string& description, std::shared_ptr<T> pVal)
        : m_section(section), m_key(key),
          m_fullKey(section.empty() ? key : (section + "." + key)),
          m_defaultValue(defaultValue), m_description(description),
          m_pValue(pVal) {}

    T Get() const {
        if (m_pValue) return *m_pValue;
        return m_defaultValue;
    }

    void Set(const T& val);

    operator T() const { return Get(); }
    T operator*() const { return Get(); }

    const std::string& GetSection() const { return m_section; }
    const std::string& GetKey() const { return m_key; }
    const std::string& GetFullKey() const { return m_fullKey; }
    const std::string& GetDescription() const { return m_description; }
    const T& GetDefault() const { return m_defaultValue; }

    void OnChange(std::function<void(const T&)> cb) {
        if (!cb) return;
        m_callbacks.push_back(cb);
    }

    void NotifyChanged(const T& val) {
        for (const auto& cb : m_callbacks) {
            try { cb(val); } catch (...) {}
        }
    }

private:
    std::string m_section;
    std::string m_key;
    std::string m_fullKey;
    T m_defaultValue{};
    std::string m_description;
    std::shared_ptr<T> m_pValue;
    std::vector<std::function<void(const T&)>> m_callbacks;
};

// ============================================================================
// DreamLiveConfig: Dynamic Runtime Configuration Registry
// Allows live tweaking of mod settings (get/set) via console without restarting
// ============================================================================
class DreamLiveConfig {
public:
    static DreamLiveConfig& Instance() {
        static DreamLiveConfig s_inst;
        return s_inst;
    }

    void RegisterBool(const std::string& key, bool defaultVal, const std::string& desc = "",
                      std::function<void(bool)> onChange = nullptr) {
        std::lock_guard<std::mutex> lock(m_mutex);
        std::string lowerKey = ToLower(key);
        auto it = m_props.find(lowerKey);
        if (it == m_props.end()) {
            ConfigProperty prop;
            prop.key = lowerKey;
            prop.type = ConfigType::Bool;
            prop.valueStr = defaultVal ? "true" : "false";
            prop.description = desc;
            if (onChange) prop.onChangeCallbacks.push_back([onChange](const std::string& val) {
                onChange(val == "true" || val == "1" || val == "on");
            });
            m_props[lowerKey] = prop;
        } else {
            if (onChange) it->second.onChangeCallbacks.push_back([onChange](const std::string& val) {
                onChange(val == "true" || val == "1" || val == "on");
            });
        }
    }

    void RegisterFloat(const std::string& key, float defaultVal, const std::string& desc = "",
                       std::function<void(float)> onChange = nullptr) {
        std::lock_guard<std::mutex> lock(m_mutex);
        std::string lowerKey = ToLower(key);
        auto it = m_props.find(lowerKey);
        if (it == m_props.end()) {
            ConfigProperty prop;
            prop.key = lowerKey;
            prop.type = ConfigType::Float;
            prop.valueStr = FloatToString(defaultVal);
            prop.description = desc;
            if (onChange) prop.onChangeCallbacks.push_back([onChange](const std::string& val) {
                onChange(static_cast<float>(std::atof(val.c_str())));
            });
            m_props[lowerKey] = prop;
        } else {
            if (onChange) it->second.onChangeCallbacks.push_back([onChange](const std::string& val) {
                onChange(static_cast<float>(std::atof(val.c_str())));
            });
        }
    }

    void RegisterInt(const std::string& key, int defaultVal, const std::string& desc = "",
                     std::function<void(int)> onChange = nullptr) {
        std::lock_guard<std::mutex> lock(m_mutex);
        std::string lowerKey = ToLower(key);
        auto it = m_props.find(lowerKey);
        if (it == m_props.end()) {
            ConfigProperty prop;
            prop.key = lowerKey;
            prop.type = ConfigType::Int;
            prop.valueStr = std::to_string(defaultVal);
            prop.description = desc;
            if (onChange) prop.onChangeCallbacks.push_back([onChange](const std::string& val) {
                onChange(std::atoi(val.c_str()));
            });
            m_props[lowerKey] = prop;
        } else {
            if (onChange) it->second.onChangeCallbacks.push_back([onChange](const std::string& val) {
                onChange(std::atoi(val.c_str()));
            });
        }
    }

    void RegisterString(const std::string& key, const std::string& defaultVal, const std::string& desc = "",
                        std::function<void(const std::string&)> onChange = nullptr) {
        std::lock_guard<std::mutex> lock(m_mutex);
        std::string lowerKey = ToLower(key);
        auto it = m_props.find(lowerKey);
        if (it == m_props.end()) {
            ConfigProperty prop;
            prop.key = lowerKey;
            prop.type = ConfigType::String;
            prop.valueStr = defaultVal;
            prop.description = desc;
            if (onChange) prop.onChangeCallbacks.push_back(onChange);
            m_props[lowerKey] = prop;
        } else {
            if (onChange) it->second.onChangeCallbacks.push_back(onChange);
        }
    }

    // ========================================================================
    // BepInEx-Style Bind<T>
    // ========================================================================
    ConfigEntry<bool> Bind(const std::string& section, const std::string& key, bool defaultVal, const std::string& desc = "") {
        std::string fullKey = section.empty() ? key : (section + "." + key);
        std::string lowerKey = ToLower(fullKey);
        auto pVal = std::make_shared<bool>(defaultVal);
        auto entry = std::make_shared<ConfigEntry<bool>>(section, key, defaultVal, desc, pVal);

        RegisterBool(lowerKey, defaultVal, desc, [pVal, entry](bool v) {
            *pVal = v;
            entry->NotifyChanged(v);
        });

        *pVal = GetBool(lowerKey, defaultVal);
        return *entry;
    }

    ConfigEntry<float> Bind(const std::string& section, const std::string& key, float defaultVal, const std::string& desc = "") {
        std::string fullKey = section.empty() ? key : (section + "." + key);
        std::string lowerKey = ToLower(fullKey);
        auto pVal = std::make_shared<float>(defaultVal);
        auto entry = std::make_shared<ConfigEntry<float>>(section, key, defaultVal, desc, pVal);

        RegisterFloat(lowerKey, defaultVal, desc, [pVal, entry](float v) {
            *pVal = v;
            entry->NotifyChanged(v);
        });

        *pVal = GetFloat(lowerKey, defaultVal);
        return *entry;
    }

    ConfigEntry<int> Bind(const std::string& section, const std::string& key, int defaultVal, const std::string& desc = "") {
        std::string fullKey = section.empty() ? key : (section + "." + key);
        std::string lowerKey = ToLower(fullKey);
        auto pVal = std::make_shared<int>(defaultVal);
        auto entry = std::make_shared<ConfigEntry<int>>(section, key, defaultVal, desc, pVal);

        RegisterInt(lowerKey, defaultVal, desc, [pVal, entry](int v) {
            *pVal = v;
            entry->NotifyChanged(v);
        });

        *pVal = GetInt(lowerKey, defaultVal);
        return *entry;
    }

    ConfigEntry<std::string> Bind(const std::string& section, const std::string& key, const std::string& defaultVal, const std::string& desc = "") {
        std::string fullKey = section.empty() ? key : (section + "." + key);
        std::string lowerKey = ToLower(fullKey);
        auto pVal = std::make_shared<std::string>(defaultVal);
        auto entry = std::make_shared<ConfigEntry<std::string>>(section, key, defaultVal, desc, pVal);

        RegisterString(lowerKey, defaultVal, desc, [pVal, entry](const std::string& v) {
            *pVal = v;
            entry->NotifyChanged(v);
        });

        std::string cur = Get(lowerKey);
        if (!cur.empty()) *pVal = cur;
        return *entry;
    }

    bool Set(const std::string& key, const std::string& valueStr) {
        std::vector<std::function<void(const std::string&)>> callbacks;
        std::string finalVal = valueStr;

        {
            std::lock_guard<std::mutex> lock(m_mutex);
            std::string lowerKey = ToLower(key);
            auto it = m_props.find(lowerKey);
            if (it == m_props.end()) return false;

            if (it->second.type == ConfigType::Bool) {
                std::string lv = ToLower(valueStr);
                finalVal = (lv == "true" || lv == "1" || lv == "on" || lv == "yes") ? "true" : "false";
            }
            it->second.valueStr = finalVal;
            callbacks = it->second.onChangeCallbacks;
        }

        for (const auto& cb : callbacks) {
            if (cb) {
                try { cb(finalVal); } catch (...) {}
            }
        }
        return true;
    }

    std::string Get(const std::string& key) const {
        std::lock_guard<std::mutex> lock(m_mutex);
        std::string lowerKey = ToLower(key);
        auto it = m_props.find(lowerKey);
        if (it != m_props.end()) {
            return it->second.valueStr;
        }
        return "";
    }

    bool GetBool(const std::string& key, bool fallback = false) const {
        std::string val = Get(key);
        if (val.empty()) return fallback;
        return val == "true" || val == "1" || val == "on";
    }

    float GetFloat(const std::string& key, float fallback = 0.0f) const {
        std::string val = Get(key);
        if (val.empty()) return fallback;
        return static_cast<float>(std::atof(val.c_str()));
    }

    int GetInt(const std::string& key, int fallback = 0) const {
        std::string val = Get(key);
        if (val.empty()) return fallback;
        return std::atoi(val.c_str());
    }

    std::vector<ConfigProperty> GetAll() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        std::vector<ConfigProperty> result;
        for (const auto& [k, v] : m_props) {
            result.push_back(v);
        }
        std::sort(result.begin(), result.end(), [](const ConfigProperty& a, const ConfigProperty& b) {
            return a.key < b.key;
        });
        return result;
    }

    bool ReloadFromFiles() {
        bool anyLoaded = false;
        DreamToml toml;

        // 1. Engine Core Config (mods/dreamkurumiHT/config.toml or local)
        std::vector<std::string> enginePaths = {
            "mods/dreamkurumiHT/config.toml",
            "dreamkurumiHT/config.toml",
            "config.toml"
        };
        for (const auto& p : enginePaths) {
            if (toml.LoadFile(p)) {
                anyLoaded = true;
                if (toml.HasSection("Backup")) {
                    bool autoBackup = toml.GetBool("Backup", "AutoBackupEnabled", true);
                    uint32_t interval = static_cast<uint32_t>(toml.GetInt("Backup", "AutoBackupIntervalMinutes", 30));
                    size_t maxBackups = static_cast<size_t>(toml.GetInt("Backup", "MaxBackupsToKeep", 10));
                    DreamBackup::Instance().StartAutoBackup(autoBackup, interval, maxBackups);
                }
                break;
            }
        }

        // 2. AoE Harvest Config
        DreamToml aoeToml;
        if (aoeToml.LoadFile("mods/Dream-AoE-Harvest/config.toml") || aoeToml.LoadFile("Dream-AoE-Harvest/config.toml")) {
            anyLoaded = true;
            if (aoeToml.HasSection("AoEHarvest")) {
                Set("aoe.enabled", aoeToml.GetBool("AoEHarvest", "Enabled", true) ? "true" : "false");
                Set("aoe.radius", FloatToString(static_cast<float>(aoeToml.GetFloat("AoEHarvest", "Radius", 3.5))));
                Set("aoe.samecrop", aoeToml.GetBool("AoEHarvest", "SameCropOnly", false) ? "true" : "false");
            }
        }

        // 3. Auto-Pickup Config
        DreamToml lootToml;
        if (lootToml.LoadFile("mods/Dream-Auto-Pickup/config.toml") || lootToml.LoadFile("Dream-Auto-Pickup/config.toml")) {
            anyLoaded = true;
            if (lootToml.HasSection("AutoPickup")) {
                Set("loot.enabled", lootToml.GetBool("AutoPickup", "Enabled", true) ? "true" : "false");
                Set("loot.radius", FloatToString(static_cast<float>(lootToml.GetFloat("AutoPickup", "PickupRadiusMeters", 5.0))));
                Set("loot.proximity", lootToml.GetBool("AutoPickup", "RemoveSpawnDelay", true) ? "true" : "false");
            }
        }

        // 4. XP-Share Config
        DreamToml xpToml;
        if (xpToml.LoadFile("mods/XP-Share/config.toml") || xpToml.LoadFile("XP-Share/config.toml")) {
            anyLoaded = true;
            if (xpToml.HasSection("Config") || xpToml.HasSection("XPShare")) {
                std::string sec = xpToml.HasSection("Config") ? "Config" : "XPShare";
                Set("xp.enabled", xpToml.GetBool(sec, "EnableXPShare", true) ? "true" : "false");
                Set("xp.multiplier", FloatToString(static_cast<float>(xpToml.GetFloat(sec, "ShareMultiplier", 0.35))));
            }
        }

        return anyLoaded;
    }

private:
    DreamLiveConfig() {
        // Register default known game / mod settings
        RegisterFloat("aoe.radius", 3.5f, "AoE Crop Harvest Radius in meters");
        RegisterBool("aoe.samecrop", true, "Harvest matching species only");
        RegisterBool("aoe.enabled", true, "AoE Crop Harvest master switch");

        RegisterFloat("loot.radius", 5.0f, "Auto Pickup Loot Radius in meters");
        RegisterBool("loot.enabled", true, "Auto Pickup master switch");
        RegisterBool("loot.proximity", true, "Instant 0s proximity pickup");

        RegisterFloat("xp.multiplier", 0.35f, "Party EXP Share ratio");
        RegisterBool("xp.enabled", true, "XP Share master switch");

        RegisterBool("admin.instantcatch", true, "Instant fishing minigame catch");
        RegisterBool("admin.enabled", true, "Admin utilities master switch");
    }

    static std::string ToLower(const std::string& s) {
        std::string res = s;
        std::transform(res.begin(), res.end(), res.begin(), ::tolower);
        return res;
    }

    static std::string FloatToString(float f) {
        std::ostringstream ss;
        ss << std::fixed << std::setprecision(2) << f;
        return ss.str();
    }

    mutable std::mutex m_mutex;
    std::unordered_map<std::string, ConfigProperty> m_props;
};

template <>
inline void ConfigEntry<bool>::Set(const bool& val) {
    if (m_pValue) {
        *m_pValue = val;
        DreamLiveConfig::Instance().Set(m_fullKey, val ? "true" : "false");
        NotifyChanged(val);
    }
}

template <>
inline void ConfigEntry<float>::Set(const float& val) {
    if (m_pValue) {
        *m_pValue = val;
        std::ostringstream ss;
        ss << std::fixed << std::setprecision(2) << val;
        DreamLiveConfig::Instance().Set(m_fullKey, ss.str());
        NotifyChanged(val);
    }
}

template <>
inline void ConfigEntry<int>::Set(const int& val) {
    if (m_pValue) {
        *m_pValue = val;
        DreamLiveConfig::Instance().Set(m_fullKey, std::to_string(val));
        NotifyChanged(val);
    }
}

template <>
inline void ConfigEntry<std::string>::Set(const std::string& val) {
    if (m_pValue) {
        *m_pValue = val;
        DreamLiveConfig::Instance().Set(m_fullKey, val);
        NotifyChanged(val);
    }
}

#define DreamConfig dreamkurumi::DreamLiveConfig::Instance()

} // namespace dreamkurumi
