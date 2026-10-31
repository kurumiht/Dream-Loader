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
#include "DreamLog.hpp"
#include "DreamToml.hpp"
#include "DreamBackup.hpp"
#include "DreamCrashGuard.hpp"
#include "DreamWatchdog.hpp"
#include "DreamRpcMultiplexer.hpp"

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

                // [General]
                if (toml.HasSection("General")) {
                    int logLvl = toml.GetInt("General", "LogLevel", 1);
                    DreamLog::Instance().SetMinLogLevel(static_cast<LogLevel>(logLvl));
                    Set("engine.loglevel", std::to_string(logLvl));
                    Set("engine.enableconsole", toml.GetBool("General", "EnableConsole", true) ? "true" : "false");
                    Set("engine.enablehotkeys", toml.GetBool("General", "EnableHotkeys", true) ? "true" : "false");
                    Set("engine.diagnosticskey", std::to_string(toml.GetInt("General", "DiagnosticsKey", 0x78)));
                    Set("engine.overlaytogglekey", std::to_string(toml.GetInt("General", "OverlayToggleKey", 0x79)));
                }

                // [Diagnostics]
                if (toml.HasSection("Diagnostics")) {
                    bool enableHud = toml.GetBool("Diagnostics", "EnableDiagnosticsHUD", true);
                    Set("diagnostics.enabled", enableHud ? "true" : "false");
                    int hotkey = toml.GetInt("Diagnostics", "DiagnosticsHotkey", 0x78);
                    Set("diagnostics.hotkey", std::to_string(hotkey));

                    DreamDiagnosticOverlay::Instance().SetEnabled(enableHud);
                    DreamDiagnosticOverlay::Instance().SetToggleKey(hotkey);
                } else if (toml.HasSection("General")) {
                    int diagKey = toml.GetInt("General", "DiagnosticsKey", 0x78);
                    DreamDiagnosticOverlay::Instance().SetToggleKey(diagKey);
                }

                // [CrashGuard]
                if (toml.HasSection("CrashGuard")) {
                    bool enableCrashGuard = toml.GetBool("CrashGuard", "EnableCrashGuard", true);
                    Set("crashguard.enabled", enableCrashGuard ? "true" : "false");

                    int threshold = toml.GetInt("CrashGuard", "CircuitBreakerThreshold", 5);
                    DreamCrashGuard::SetCircuitThreshold(static_cast<uint32_t>(threshold));
                    Set("crashguard.threshold", std::to_string(threshold));

                    bool toast = toml.GetBool("CrashGuard", "EnableToastAlert", true);
                    Set("crashguard.toast", toast ? "true" : "false");

                    float dur = static_cast<float>(toml.GetFloat("CrashGuard", "ToastDurationSeconds", 3.5));
                    Set("crashguard.toastduration", FloatToString(dur));
                }

                // [Network.Rpc]
                if (toml.HasSection("Network.Rpc")) {
                    double refill = toml.GetFloat("Network.Rpc", "CallsPerSecondLimit", 120.0);
                    double burst = toml.GetFloat("Network.Rpc", "MaxBurstCapacity", 120.0);
                    DreamRpcMultiplexer::Instance().SetTokenBucketParams(refill, burst);
                    Set("rpc.calls_per_sec", FloatToString(static_cast<float>(refill)));
                    Set("rpc.max_burst", FloatToString(static_cast<float>(burst)));
                }

                // [Watchdog]
                if (toml.HasSection("Watchdog")) {
                    bool enableWatchdog = toml.GetBool("Watchdog", "EnableWatchdog", true);
                    uint32_t hangTimeout = static_cast<uint32_t>(toml.GetInt("Watchdog", "HangTimeoutSeconds", 15));
                    Set("watchdog.enabled", enableWatchdog ? "true" : "false");
                    Set("watchdog.timeout", std::to_string(hangTimeout));
                    if (enableWatchdog) {
                        DreamWatchdog::Instance().Start(500, hangTimeout * 1000);
                    } else {
                        DreamWatchdog::Instance().Stop();
                    }
                }

                // [Backup]
                if (toml.HasSection("Backup")) {
                    bool autoBackup = toml.GetBool("Backup", "AutoBackupEnabled", true);
                    uint32_t interval = static_cast<uint32_t>(toml.GetInt("Backup", "AutoBackupIntervalMinutes", 30));
                    size_t maxBackups = static_cast<size_t>(toml.GetInt("Backup", "MaxBackupsToKeep", 10));
                    DreamBackup::Instance().StartAutoBackup(autoBackup, interval, maxBackups);
                }
                break;
            }
        }

        return anyLoaded;
    }

private:
    DreamLiveConfig() {
        // Engine defaults
        RegisterInt("engine.loglevel", 1, "Log level [0=Silent, 1=Info, 2=Debug, 3=Trace]");
        RegisterBool("engine.enableconsole", true, "Allocate console window");
        RegisterBool("engine.enablehotkeys", true, "Enable client hotkeys");

        // CrashGuard defaults
        RegisterBool("crashguard.enabled", true, "CrashGuard sandbox master switch");
        RegisterInt("crashguard.threshold", 5, "Consecutive crashes before circuit trip");
        RegisterBool("crashguard.toast", true, "Enable in-game toast notifications");
        RegisterFloat("crashguard.toastduration", 3.5f, "Toast duration in seconds");

        // Diagnostics HUD defaults
        RegisterBool("diagnostics.enabled", true, "Enable Diagnostics HUD");
        RegisterInt("diagnostics.hotkey", 0x78, "Diagnostics HUD Hotkey (F9)");

        // RPC defaults
        RegisterFloat("rpc.calls_per_sec", 120.0f, "RPC rate limit per caller");
        RegisterFloat("rpc.max_burst", 120.0f, "RPC max burst tokens");

        // Watchdog defaults
        RegisterBool("watchdog.enabled", true, "Watchdog anti-hang monitor");
        RegisterInt("watchdog.timeout", 15, "Hang detection timeout in seconds");
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
