#pragma once
#include <windows.h>
#include <string>
#include <vector>
#include <unordered_map>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <mutex>
#include "DreamLog.hpp"
#include "DreamWatcher.hpp"

namespace dreamkurumi {

enum class EnvironmentSide {
    Client,
    DedicatedServer
};

// ============================================================================
// DreamModMetadata - Mod Manifest and Developer Registration
// ============================================================================
struct DreamModMetadata {
    std::string id;
    std::string name;
    std::string displayName;
    std::string version{"1.0.0"};
    std::string author;
    std::string description;
    std::string website;
    std::string directory;
};

// ============================================================================
// DreamEnv - Mod Developer Environment & Self-Discovery Engine
// Gives mod developers 100% path freedom:
// Auto-discovers mod DLL location regardless of folder name or depth.
// Solves Windows CWD (Current Working Directory) traps once and for all.
// Automatic Dual-Side Detection (Client vs Dedicated Server).
// ModInfo.xml / info.xml Manifest Auto-Discovery & Zero-Dependency Parsing.
// ============================================================================
class DreamEnv {
public:
    // Returns true if running inside Dedicated Server (enshrouded_server.exe or headless server process)
    static bool IsDedicatedServer() {
        static bool s_checked = false;
        static bool s_isServer = false;
        if (!s_checked) {
            char path[MAX_PATH] = {0};
            if (GetModuleFileNameA(NULL, path, MAX_PATH)) {
                std::string exeName = path;
                for (char& c : exeName) c = static_cast<char>(::tolower(c));
                if (exeName.find("enshrouded_server") != std::string::npos ||
                    exeName.find("server") != std::string::npos) {
                    s_isServer = true;
                }
            }
            s_checked = true;
        }
        return s_isServer;
    }

    // Returns true if running inside Game Client (enshrouded.exe)
    static bool IsClient() {
        return !IsDedicatedServer();
    }

    // Returns whether current execution is on Client or DedicatedServer
    static EnvironmentSide GetEnvironmentSide() {
        return IsDedicatedServer() ? EnvironmentSide::DedicatedServer : EnvironmentSide::Client;
    }

    // Returns friendly string representation ("DedicatedServer" or "Client")
    static const char* GetEnvironmentName() {
        return IsDedicatedServer() ? "DedicatedServer" : "Client";
    }

    // Returns the HMODULE of the calling DLL/Module automatically
    static HMODULE GetCallingModule(void* returnAddress = nullptr) {
        if (!returnAddress) {
            returnAddress = reinterpret_cast<void*>(&GetCallingModule);
        }
        HMODULE hMod = NULL;
        GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCSTR>(returnAddress), &hMod);
        return hMod ? hMod : GetModuleHandleA(NULL);
    }

    // Returns the absolute directory containing the calling mod DLL
    // Example: "H:/SteamLibrary/steamapps/common/Enshrouded/mods/MyMod/"
    static std::filesystem::path GetModDirectory(HMODULE hMod = NULL) {
        if (!hMod) {
            hMod = GetCallingModule();
        }
        char pathBuf[MAX_PATH] = {0};
        if (GetModuleFileNameA(hMod, pathBuf, MAX_PATH)) {
            std::filesystem::path p(pathBuf);
            return p.parent_path();
        }
        return std::filesystem::current_path();
    }

    // Returns the root directory of the game (containing enshrouded.exe)
    static std::filesystem::path GetGameDirectory() {
        char pathBuf[MAX_PATH] = {0};
        if (GetModuleFileNameA(NULL, pathBuf, MAX_PATH)) {
            std::filesystem::path p(pathBuf);
            return p.parent_path();
        }
        return std::filesystem::current_path();
    }

    // Returns the standard mods directory: "<GameDir>/mods"
    static std::filesystem::path GetModsDirectory() {
        return GetGameDirectory() / "mods";
    }

    // Resolves a relative file path:
    // 1. Checks if it exists inside the Mod's own folder (<ModDir>/relative)
    // 2. Checks if it exists in the Game's root folder (<GameDir>/relative)
    // 3. Fallbacks to <ModDir>/relative for new file creation
    static std::filesystem::path ResolvePath(const std::string& relativePath, HMODULE hMod = NULL) {
        auto modDir = GetModDirectory(hMod);
        auto modPath = modDir / relativePath;
        if (std::filesystem::exists(modPath)) {
            return modPath;
        }

        auto gameDir = GetGameDirectory();
        auto gamePath = gameDir / relativePath;
        if (std::filesystem::exists(gamePath)) {
            return gamePath;
        }

        return modPath;
    }

    // Resolves a config path guaranteed to be in the Mod's own directory
    static std::filesystem::path GetModConfigPath(const std::string& filename = "config.ini", HMODULE hMod = NULL) {
        return GetModDirectory(hMod) / filename;
    }

    // ========================================================================
    // ModInfo.xml / info.xml Support (Zero External Dependencies)
    // ========================================================================

    // Extracts an XML tag attribute (value="...") or inner element text (<tag>...</tag>)
    static std::string ExtractXmlValue(const std::string& xml, const std::string& tag) {
        size_t tagPos = 0;
        while ((tagPos = xml.find("<" + tag, tagPos)) != std::string::npos) {
            size_t closeTag = xml.find('>', tagPos);
            if (closeTag == std::string::npos) break;

            std::string tagChunk = xml.substr(tagPos, closeTag - tagPos + 1);
            size_t valPos = tagChunk.find("value=\"");
            if (valPos != std::string::npos) {
                size_t start = valPos + 7;
                size_t end = tagChunk.find('"', start);
                if (end != std::string::npos) {
                    return tagChunk.substr(start, end - start);
                }
            }
            valPos = tagChunk.find("value='");
            if (valPos != std::string::npos) {
                size_t start = valPos + 7;
                size_t end = tagChunk.find('\'', start);
                if (end != std::string::npos) {
                    return tagChunk.substr(start, end - start);
                }
            }

            // Inner element text: <Tag>Value</Tag>
            if (tagChunk.size() >= 2 && tagChunk[tagChunk.size() - 2] != '/') {
                std::string closeTagStr = "</" + tag + ">";
                size_t endTagPos = xml.find(closeTagStr, closeTag);
                if (endTagPos != std::string::npos) {
                    return xml.substr(closeTag + 1, endTagPos - (closeTag + 1));
                }
            }
            tagPos = closeTag + 1;
        }
        return "";
    }

    // Parses raw XML content into a structured DreamModMetadata object
    static DreamModMetadata ParseModInfoXml(const std::string& xmlContent) {
        DreamModMetadata meta;
        meta.id = ExtractXmlValue(xmlContent, "Name");
        meta.displayName = ExtractXmlValue(xmlContent, "DisplayName");
        meta.name = !meta.displayName.empty() ? meta.displayName : meta.id;
        meta.version = ExtractXmlValue(xmlContent, "Version");
        if (meta.version.empty()) meta.version = "1.0.0";
        meta.description = ExtractXmlValue(xmlContent, "Description");
        meta.author = ExtractXmlValue(xmlContent, "Author");
        meta.website = ExtractXmlValue(xmlContent, "Website");
        return meta;
    }

    // ========================================================================
    // modinfo.json / manifest.json Support (Modern Standard, Zero Dependencies)
    // ========================================================================

    // Extracts a string or value from simple JSON: "key": "value"
    static std::string ExtractJsonValue(const std::string& json, const std::string& key) {
        std::vector<std::string> patterns = { "\"" + key + "\"", "'" + key + "'" };
        size_t keyPos = std::string::npos;
        for (const auto& pat : patterns) {
            keyPos = json.find(pat);
            if (keyPos != std::string::npos) {
                keyPos += pat.size();
                break;
            }
        }
        if (keyPos == std::string::npos) return "";

        size_t colonPos = json.find(':', keyPos);
        if (colonPos == std::string::npos) return "";

        size_t valStart = colonPos + 1;
        while (valStart < json.size() && (json[valStart] == ' ' || json[valStart] == '\t' || json[valStart] == '\r' || json[valStart] == '\n')) {
            valStart++;
        }
        if (valStart >= json.size()) return "";

        if (json[valStart] == '"' || json[valStart] == '\'') {
            char quoteChar = json[valStart];
            size_t endQuote = valStart + 1;
            while (endQuote < json.size()) {
                if (json[endQuote] == quoteChar && json[endQuote - 1] != '\\') break;
                endQuote++;
            }
            if (endQuote < json.size()) {
                return json.substr(valStart + 1, endQuote - (valStart + 1));
            }
        } else {
            size_t endVal = json.find_first_of(",}\r\n", valStart);
            if (endVal == std::string::npos) endVal = json.size();
            std::string rawVal = json.substr(valStart, endVal - valStart);
            while (!rawVal.empty() && (rawVal.back() == ' ' || rawVal.back() == '\t')) rawVal.pop_back();
            return rawVal;
        }
        return "";
    }

    // Parses raw JSON content into a structured DreamModMetadata object
    static DreamModMetadata ParseModInfoJson(const std::string& jsonContent) {
        DreamModMetadata meta;
        meta.id = ExtractJsonValue(jsonContent, "id");
        if (meta.id.empty()) meta.id = ExtractJsonValue(jsonContent, "name");
        if (meta.id.empty()) meta.id = ExtractJsonValue(jsonContent, "Name");

        meta.displayName = ExtractJsonValue(jsonContent, "displayName");
        if (meta.displayName.empty()) meta.displayName = ExtractJsonValue(jsonContent, "DisplayName");

        meta.name = !meta.displayName.empty() ? meta.displayName : meta.id;

        meta.version = ExtractJsonValue(jsonContent, "version");
        if (meta.version.empty()) meta.version = ExtractJsonValue(jsonContent, "Version");
        if (meta.version.empty()) meta.version = "1.0.0";

        meta.description = ExtractJsonValue(jsonContent, "description");
        if (meta.description.empty()) meta.description = ExtractJsonValue(jsonContent, "Description");

        meta.author = ExtractJsonValue(jsonContent, "author");
        if (meta.author.empty()) meta.author = ExtractJsonValue(jsonContent, "Author");

        meta.website = ExtractJsonValue(jsonContent, "website");
        if (meta.website.empty()) meta.website = ExtractJsonValue(jsonContent, "Website");

        return meta;
    }

    // Checks if any supported mod manifest exists (JSON or XML)
    static bool HasModManifest(HMODULE hMod = NULL) {
        auto modDir = GetModDirectory(hMod);
        return std::filesystem::exists(modDir / "modinfo.json") ||
               std::filesystem::exists(modDir / "manifest.json") ||
               std::filesystem::exists(modDir / "info.json") ||
               std::filesystem::exists(modDir / "ModInfo.xml") ||
               std::filesystem::exists(modDir / "modinfo.xml") ||
               std::filesystem::exists(modDir / "info.xml");
    }

    // Checks if ModInfo.xml / info.xml exists in the mod's directory
    static bool HasModInfoXml(HMODULE hMod = NULL) {
        return HasModManifest(hMod);
    }

    // Loads mod manifest from JSON (modinfo.json / manifest.json) or XML (ModInfo.xml / info.xml) if present,
    // or gracefully falls back to default DLL stem name if absent (manifest is 100% optional!)
    static DreamModMetadata LoadModInfo(HMODULE hMod = NULL) {
        if (!hMod) hMod = GetCallingModule();
        auto modDir = GetModDirectory(hMod);

        // 1. Modern JSON candidates (preferred for modern tooling)
        const std::vector<std::string> jsonCandidates = {"modinfo.json", "manifest.json", "info.json"};
        for (const auto& fname : jsonCandidates) {
            auto path = modDir / fname;
            if (std::filesystem::exists(path)) {
                std::ifstream f(path);
                if (f.is_open()) {
                    std::stringstream ss;
                    ss << f.rdbuf();
                    auto meta = ParseModInfoJson(ss.str());
                    meta.directory = modDir.string();
                    if (meta.id.empty()) {
                        meta.id = path.parent_path().stem().string();
                    }
                    DREAM_LOG_INFO("DreamEnv: Loaded mod manifest from '%s' [JSON] (ID='%s', Name='%s', Version='%s')",
                                   fname.c_str(), meta.id.c_str(), meta.name.c_str(), meta.version.c_str());
                    return meta;
                }
            }
        }

        // 2. Legacy / XML candidates
        const std::vector<std::string> xmlCandidates = {"ModInfo.xml", "modinfo.xml", "info.xml"};
        for (const auto& fname : xmlCandidates) {
            auto path = modDir / fname;
            if (std::filesystem::exists(path)) {
                std::ifstream f(path);
                if (f.is_open()) {
                    std::stringstream ss;
                    ss << f.rdbuf();
                    auto meta = ParseModInfoXml(ss.str());
                    meta.directory = modDir.string();
                    if (meta.id.empty()) {
                        meta.id = path.parent_path().stem().string();
                    }
                    DREAM_LOG_INFO("DreamEnv: Loaded mod manifest from '%s' [XML] (ID='%s', Name='%s', Version='%s')",
                                   fname.c_str(), meta.id.c_str(), meta.name.c_str(), meta.version.c_str());
                    return meta;
                }
            }
        }

        // 3. Graceful Fallback: Mod functions 100% fine without any manifest!
        DreamModMetadata fallback;
        char pathBuf[MAX_PATH] = {0};
        if (GetModuleFileNameA(hMod, pathBuf, MAX_PATH)) {
            std::filesystem::path p(pathBuf);
            fallback.id = p.stem().string();
            fallback.name = fallback.id;
        } else {
            fallback.id = "MyDreamMod";
            fallback.name = "My Dream Mod";
        }
        fallback.version = "1.0.0";
        fallback.author = "Unknown";
        fallback.directory = modDir.string();
        return fallback;
    }

    // Saves mod manifest to ModInfo.xml format
    static bool SaveModInfoXml(const DreamModMetadata& meta, const std::string& filename = "ModInfo.xml", HMODULE hMod = NULL) {
        auto outPath = GetModDirectory(hMod) / filename;
        std::ofstream f(outPath);
        if (!f.is_open()) return false;
        f << "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n";
        f << "<xml>\n";
        f << "  <Name value=\"" << (meta.id.empty() ? meta.name : meta.id) << "\" />\n";
        if (!meta.displayName.empty()) {
            f << "  <DisplayName value=\"" << meta.displayName << "\" />\n";
        }
        f << "  <Version value=\"" << (meta.version.empty() ? "1.0.0" : meta.version) << "\" />\n";
        if (!meta.description.empty()) {
            f << "  <Description value=\"" << meta.description << "\" />\n";
        }
        if (!meta.author.empty()) {
            f << "  <Author value=\"" << meta.author << "\" />\n";
        }
        if (!meta.website.empty()) {
            f << "  <Website value=\"" << meta.website << "\" />\n";
        }
        f << "</xml>\n";
        return true;
    }

    // Saves mod manifest to modern modinfo.json format
    static bool SaveModInfoJson(const DreamModMetadata& meta, const std::string& filename = "modinfo.json", HMODULE hMod = NULL) {
        auto outPath = GetModDirectory(hMod) / filename;
        std::ofstream f(outPath);
        if (!f.is_open()) return false;
        f << "{\n";
        f << "  \"name\": \"" << (meta.id.empty() ? meta.name : meta.id) << "\",\n";
        if (!meta.displayName.empty()) {
            f << "  \"displayName\": \"" << meta.displayName << "\",\n";
        }
        f << "  \"version\": \"" << (meta.version.empty() ? "1.0.0" : meta.version) << "\",\n";
        f << "  \"author\": \"" << (meta.author.empty() ? "Unknown" : meta.author) << "\",\n";
        f << "  \"description\": \"" << meta.description << "\",\n";
        f << "  \"website\": \"" << meta.website << "\"\n";
        f << "}\n";
        return true;
    }
};

// ============================================================================
// DreamModConfig - Ultra-Lightweight, Self-Contained INI Config Manager
// Dedicated to Mod Developers. Zero dependencies, thread-safe, auto-persisting.
// Automatically targets the mod's own folder and supports instant hot-reloading!
// ============================================================================
class DreamModConfig {
public:
    DreamModConfig() = default;

    explicit DreamModConfig(const std::string& filename, HMODULE hMod = NULL) {
        m_filePath = DreamEnv::GetModConfigPath(filename, hMod).string();
        Load();
    }

    bool Load(const std::string& overridePath = "") {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!overridePath.empty()) {
            m_filePath = overridePath;
        }
        if (m_filePath.empty()) return false;

        std::ifstream file(m_filePath);
        if (!file.is_open()) return false;

        m_data.clear();
        std::string currentSection = "General";
        std::string line;

        while (std::getline(file, line)) {
            // Trim whitespace
            size_t start = line.find_first_not_of(" \t\r\n");
            if (start == std::string::npos) continue;
            size_t end = line.find_last_not_of(" \t\r\n");
            line = line.substr(start, end - start + 1);

            if (line.empty() || line[0] == '#' || line[0] == ';') continue;

            if (line.front() == '[' && line.back() == ']') {
                currentSection = line.substr(1, line.length() - 2);
                continue;
            }

            size_t eqPos = line.find('=');
            if (eqPos != std::string::npos) {
                std::string key = line.substr(0, eqPos);
                std::string val = line.substr(eqPos + 1);

                // Trim key & val
                key = Trim(key);
                val = Trim(val);

                m_data[currentSection][key] = val;
            }
        }
        return true;
    }

    bool Save() {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_filePath.empty()) return false;

        std::ofstream file(m_filePath);
        if (!file.is_open()) return false;

        file << "# dreamkurumiHT Mod Configuration\n\n";
        for (const auto& [sec, keys] : m_data) {
            file << "[" << sec << "]\n";
            for (const auto& [k, v] : keys) {
                file << k << "=" << v << "\n";
            }
            file << "\n";
        }
        return true;
    }

    std::string GetString(const std::string& section, const std::string& key, const std::string& defaultVal = "") {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto sIt = m_data.find(section);
        if (sIt != m_data.end()) {
            auto kIt = sIt->second.find(key);
            if (kIt != sIt->second.end()) return kIt->second;
        }
        return defaultVal;
    }

    int GetInt(const std::string& section, const std::string& key, int defaultVal = 0) {
        std::string val = GetString(section, key, "");
        if (val.empty()) return defaultVal;
        try { return std::stoi(val); } catch (...) { return defaultVal; }
    }

    float GetFloat(const std::string& section, const std::string& key, float defaultVal = 0.0f) {
        std::string val = GetString(section, key, "");
        if (val.empty()) return defaultVal;
        try { return std::stof(val); } catch (...) { return defaultVal; }
    }

    bool GetBool(const std::string& section, const std::string& key, bool defaultVal = false) {
        std::string val = GetString(section, key, "");
        if (val.empty()) return defaultVal;
        return (val == "1" || val == "true" || val == "True" || val == "yes" || val == "on");
    }

    void SetString(const std::string& section, const std::string& key, const std::string& val) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_data[section][key] = val;
    }

    void SetInt(const std::string& section, const std::string& key, int val) {
        SetString(section, key, std::to_string(val));
    }

    void SetFloat(const std::string& section, const std::string& key, float val) {
        SetString(section, key, std::to_string(val));
    }

    void SetBool(const std::string& section, const std::string& key, bool val) {
        SetString(section, key, val ? "true" : "false");
    }

    // Connects to DreamWatcher for live hot-reloading when config file is edited
    void EnableHotReload(std::function<void()> onReloadCallback = nullptr) {
        if (m_filePath.empty()) return;
        DreamWatcher::Instance().WatchFile(m_filePath, [this, onReloadCallback](const std::string&) {
            Load();
            DREAM_LOG_INFO("DreamModConfig: Live reloaded '%s'", m_filePath.c_str());
            if (onReloadCallback) onReloadCallback();
        });
    }

    const std::string& GetFilePath() const { return m_filePath; }

private:
    static std::string Trim(const std::string& str) {
        size_t start = str.find_first_not_of(" \t\r\n");
        if (start == std::string::npos) return "";
        size_t end = str.find_last_not_of(" \t\r\n");
        return str.substr(start, end - start + 1);
    }

    std::string m_filePath;
    std::unordered_map<std::string, std::unordered_map<std::string, std::string>> m_data;
    std::mutex m_mutex;
};

// ============================================================================
// DreamSemVer - Semantic Versioning (SemVer 2.0) Parser & Comparator
// Supports: "1.2.3", "v2.0.0", "1.0", "2.1.0-beta", "1.0.0.4"
// ============================================================================
struct DreamSemVer {
    int major{0};
    int minor{0};
    int patch{0};
    int build{0};
    std::string prerelease;
    std::string raw;

    static DreamSemVer Parse(const std::string& versionStr) {
        DreamSemVer v;
        v.raw = versionStr;
        std::string s = versionStr;

        // Strip leading 'v' or 'V'
        if (!s.empty() && (s[0] == 'v' || s[0] == 'V')) {
            s = s.substr(1);
        }

        // Check for prerelease suffix (e.g. -beta, -rc1)
        size_t dashPos = s.find('-');
        if (dashPos != std::string::npos) {
            v.prerelease = s.substr(dashPos + 1);
            s = s.substr(0, dashPos);
        }

        // Split numbers by '.'
        std::vector<int> parts;
        std::stringstream ss(s);
        std::string token;
        while (std::getline(ss, token, '.')) {
            try {
                parts.push_back(std::stoi(token));
            } catch (...) {
                parts.push_back(0);
            }
        }

        if (parts.size() > 0) v.major = parts[0];
        if (parts.size() > 1) v.minor = parts[1];
        if (parts.size() > 2) v.patch = parts[2];
        if (parts.size() > 3) v.build = parts[3];

        return v;
    }

    bool operator<(const DreamSemVer& o) const {
        if (major != o.major) return major < o.major;
        if (minor != o.minor) return minor < o.minor;
        if (patch != o.patch) return patch < o.patch;
        if (build != o.build) return build < o.build;
        // Prerelease version is lower precedence than release (e.g. 1.0.0-beta < 1.0.0)
        if (!prerelease.empty() && o.prerelease.empty()) return true;
        if (prerelease.empty() && !o.prerelease.empty()) return false;
        return prerelease < o.prerelease;
    }

    bool operator>(const DreamSemVer& o) const { return o < *this; }
    bool operator==(const DreamSemVer& o) const {
        return major == o.major && minor == o.minor && patch == o.patch && build == o.build && prerelease == o.prerelease;
    }
    bool operator!=(const DreamSemVer& o) const { return !(*this == o); }
    bool operator<=(const DreamSemVer& o) const { return !(o < *this); }
    bool operator>=(const DreamSemVer& o) const { return !(*this < o); }

    std::string ToString() const {
        std::stringstream ss;
        ss << major << "." << minor << "." << patch;
        if (build > 0) ss << "." << build;
        if (!prerelease.empty()) ss << "-" << prerelease;
        return ss.str();
    }
};

enum class ModRegisterStatus {
    Activated,               // First instance registered and active
    UpgradedNewer,           // Incoming is NEWER than existing: took over active role
    RejectedOlderDuplicate   // Incoming is OLDER or EQUAL duplicate: rejected / suppressed
};

struct ModRegisterResult {
    ModRegisterStatus status;
    bool isAllowedToHook;    // true if this instance should proceed with hooking
    std::string activeVersion;
    std::string message;
};

// ============================================================================
// DreamModRegistry - Version-Aware Conflict Resolver & Mod Manifest Registry
// Prevents duplicate mod hooks, infinite loops, and selects the newest version!
// ============================================================================
class DreamModRegistry {
public:
    static DreamModRegistry& Instance() {
        static DreamModRegistry s_instance;
        return s_instance;
    }

    // Auto-discovers and registers mod from ModInfo.xml / info.xml if available,
    // or falls back to DLL stem name. XML is 100% optional!
    ModRegisterResult RegisterAuto(HMODULE hMod = NULL) {
        auto meta = DreamEnv::LoadModInfo(hMod);
        return Register(meta);
    }

    ModRegisterResult Register(const DreamModMetadata& meta) {
        std::lock_guard<std::mutex> lock(m_mutex);
        DreamSemVer incomingVer = DreamSemVer::Parse(meta.version);

        auto it = m_mods.find(meta.id);
        if (it == m_mods.end()) {
            // First time registration: Activate
            m_mods[meta.id] = meta;
            DREAM_LOG_INFO("DreamModRegistry: [MOD ACTIVATED] Registered mod '%s' (v%s) by %s",
                           meta.name.c_str(), meta.version.c_str(), meta.author.c_str());
            return {
                ModRegisterStatus::Activated,
                true,
                meta.version,
                "Activated as primary mod instance."
            };
        }

        // Mod ID already registered: Compare versions!
        DreamSemVer existingVer = DreamSemVer::Parse(it->second.version);

        if (incomingVer > existingVer) {
            // Incoming mod is NEWER! Supersede older version!
            std::string oldVer = it->second.version;
            it->second = meta; // Replace with newer metadata
            DREAM_LOG_WARN("DreamModRegistry: [MOD UPGRADE] Mod '%s' superseded older v%s with NEWER v%s!",
                           meta.name.c_str(), oldVer.c_str(), meta.version.c_str());
            return {
                ModRegisterStatus::UpgradedNewer,
                true,
                meta.version,
                "Incoming version is newer; upgraded successfully."
            };
        } else {
            // Incoming mod is OLDER or EQUAL duplicate: REJECT to prevent duplicate hooks!
            DREAM_LOG_WARN("DreamModRegistry: [MOD CONFLICT BLOCKED] Duplicate mod '%s' v%s rejected! Active version v%s is newer or already loaded. Skipping duplicate hooks to prevent crash.",
                           meta.name.c_str(), meta.version.c_str(), it->second.version.c_str());
            return {
                ModRegisterStatus::RejectedOlderDuplicate,
                false,
                it->second.version,
                "Duplicate or older version rejected; active version retained."
            };
        }
    }

    // Fast one-line guard for DllMain / Mod initialization:
    // Returns true if this mod instance is the newest and allowed to hook, false if older duplicate
    static bool ShouldInstallHooks(const std::string& id, const std::string& name,
                                   const std::string& version, const std::string& author = "") {
        auto res = Instance().Register({
            .id = id,
            .name = name,
            .version = version,
            .author = author,
            .directory = DreamEnv::GetModDirectory().string()
        });
        return res.isAllowedToHook;
    }

    std::vector<DreamModMetadata> GetAllMods() {
        std::lock_guard<std::mutex> lock(m_mutex);
        std::vector<DreamModMetadata> result;
        for (const auto& [id, mod] : m_mods) {
            result.push_back(mod);
        }
        return result;
    }

    bool HasMod(const std::string& id) {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_mods.find(id) != m_mods.end();
    }

    std::string GetActiveVersion(const std::string& id) {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_mods.find(id);
        return (it != m_mods.end()) ? it->second.version : "";
    }

    // Scans a folder (e.g. mods/) and detects potential duplicate mod DLLs or older files
    static std::vector<std::string> DetectDuplicateFiles(const std::filesystem::path& folder) {
        std::vector<std::string> duplicates;
        if (!std::filesystem::exists(folder)) return duplicates;

        std::unordered_map<std::string, std::vector<std::filesystem::path>> nameMap;
        for (const auto& entry : std::filesystem::recursive_directory_iterator(folder)) {
            if (entry.is_regular_file() && entry.path().extension() == ".dll") {
                std::string stem = entry.path().stem().string();
                nameMap[stem].push_back(entry.path());
            }
        }

        for (const auto& [name, paths] : nameMap) {
            if (paths.size() > 1) {
                std::string msg = "Duplicate DLLs found for '" + name + "': ";
                for (const auto& p : paths) msg += p.string() + " ; ";
                duplicates.push_back(msg);
                DREAM_LOG_WARN("DreamModRegistry: %s", msg.c_str());
            }
        }
        return duplicates;
    }

private:
    DreamModRegistry() = default;
    std::unordered_map<std::string, DreamModMetadata> m_mods;
    std::mutex m_mutex;
};

} // namespace dreamkurumi

// Global convenience macros for dual-side mod branching
#define DREAM_SERVER_ONLY(code) do { if (::dreamkurumi::DreamEnv::IsDedicatedServer()) { code; } } while(0)
#define DREAM_CLIENT_ONLY(code) do { if (::dreamkurumi::DreamEnv::IsClient()) { code; } } while(0)

// ============================================================================
// Standard Enshrouded Native Plugin ABI (XHL) & Zero-Boilerplate Mod Entry
// ============================================================================
namespace dreamkurumi {

#pragma pack(push, 1)
struct XhlPluginQueryInfo {
    uint32_t structSize;
    uint32_t abiVersion;
    uint32_t reserved;
    char id[64];
    char name[128];
    char version[32];
};
#pragma pack(pop)

struct XhlPluginContext {
    uint32_t structSize;
    uint32_t apiVersion;
    HMODULE gameModule;
    uint64_t reserved;
    uint32_t processId;
    uint32_t padding;
    void (*logCallback)(const char* utf8Message);
};

} // namespace dreamkurumi

// Macro for 100% Zero-Boilerplate Mod Entry (auto-generates all XHL exports & DllMain)
#define DREAM_MOD_ENTRY(OnStartCode, OnStopCode) \
extern "C" { \
__declspec(dllexport) int XhlNativePluginQuery(::dreamkurumi::XhlPluginQueryInfo* info) { \
    if (!info) return 0; \
    std::memset(info, 0, sizeof(::dreamkurumi::XhlPluginQueryInfo)); \
    info->structSize = sizeof(::dreamkurumi::XhlPluginQueryInfo); \
    info->abiVersion = 0x10000; \
    auto meta = ::dreamkurumi::DreamEnv::LoadModInfo(); \
    ::dreamkurumi::DreamModRegistry::Instance().Register(meta); \
    strncpy(info->id, meta.id.c_str(), sizeof(info->id) - 1); \
    strncpy(info->name, meta.name.c_str(), sizeof(info->name) - 1); \
    strncpy(info->version, meta.version.c_str(), sizeof(info->version) - 1); \
    return 1; \
} \
__declspec(dllexport) int XhlNativePluginStart(::dreamkurumi::XhlPluginContext* ctx) { \
    if (ctx && ctx->logCallback) { \
        ::dreamkurumi::DreamLog::Instance().SetExternalCallback(ctx->logCallback); \
    } \
    if (!::dreamkurumi::DreamEnv::IsDedicatedServer()) { \
        ::dreamkurumi::DreamLog::Instance().AllocateConsoleWindow("dreamkurumiHT Diagnostics Console (Client)"); \
    } \
    ::dreamkurumi::DreamCache::Instance().Initialize("dreamkurumi_cache.ini"); \
    size_t installedHooks = ::dreamkurumi::DreamHarmony::PatchAll(); \
    DREAM_LOG_INFO("dreamkurumiHT: Initialized mod with %zu active hooks", installedHooks); \
    ::dreamkurumi::DreamOverlay::Instance().StartHotkeyWatcher(); \
    ::dreamkurumi::DreamOverlay::Instance().StartConsoleInputListener(); \
    { OnStartCode; } \
    return 1; \
} \
__declspec(dllexport) void XhlNativePluginRequestStop() { \
    { OnStopCode; } \
    ::dreamkurumi::DreamOverlay::Instance().StopHotkeyWatcher(); \
    ::dreamkurumi::DreamOverlay::Instance().StopConsoleInputListener(); \
    ::dreamkurumi::DreamHarmony::UnpatchAll(); \
    ::dreamkurumi::DreamLog::Instance().FreeConsoleWindow(); \
} \
} \
BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) { \
    return TRUE; \
}
