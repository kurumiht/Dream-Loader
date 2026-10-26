#pragma once
#include <string>
#include <unordered_map>
#include <vector>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cstdint>
#include <cctype>

namespace dreamkurumi {

// ============================================================================
// DreamToml - Lightweight, Zero-Dependency C++20 TOML Parser
// Supports sections, typed keys, string quotes, arrays, hex, and UTF-8 bilingual comments
// ============================================================================
class DreamToml {
public:
    struct Value {
        std::string raw;

        bool AsBool(bool def = false) const {
            std::string s = ToLower(Trim(raw));
            if (s == "true" || s == "1" || s == "on" || s == "yes") return true;
            if (s == "false" || s == "0" || s == "off" || s == "no") return false;
            return def;
        }

        int64_t AsInt(int64_t def = 0) const {
            std::string s = Trim(raw);
            if (s.empty()) return def;
            try {
                if (s.rfind("0x", 0) == 0 || s.rfind("0X", 0) == 0) {
                    return std::stoll(s, nullptr, 16);
                }
                return std::stoll(s, nullptr, 10);
            } catch (...) {
                return def;
            }
        }

        double AsFloat(double def = 0.0) const {
            std::string s = Trim(raw);
            if (s.empty()) return def;
            try {
                return std::stod(s);
            } catch (...) {
                return def;
            }
        }

        std::string AsString(const std::string& def = "") const {
            std::string s = Trim(raw);
            if (s.size() >= 2 && ((s.front() == '"' && s.back() == '"') || (s.front() == '\'' && s.back() == '\''))) {
                return s.substr(1, s.size() - 2);
            }
            return s.empty() ? def : s;
        }

        std::vector<std::string> AsStringArray() const {
            std::vector<std::string> result;
            std::string s = Trim(raw);
            if (s.size() >= 2 && s.front() == '[' && s.back() == ']') {
                std::string inner = s.substr(1, s.size() - 2);
                std::stringstream ss(inner);
                std::string item;
                while (std::getline(ss, item, ',')) {
                    std::string trimmed = Trim(item);
                    if (trimmed.size() >= 2 && ((trimmed.front() == '"' && trimmed.back() == '"') || (trimmed.front() == '\'' && trimmed.back() == '\''))) {
                        trimmed = trimmed.substr(1, trimmed.size() - 2);
                    }
                    if (!trimmed.empty()) {
                        result.push_back(trimmed);
                    }
                }
            }
            return result;
        }

    private:
        static std::string Trim(const std::string& str) {
            size_t first = str.find_first_not_of(" \t\r\n");
            if (first == std::string::npos) return "";
            size_t last = str.find_last_not_of(" \t\r\n");
            return str.substr(first, (last - first + 1));
        }

        static std::string ToLower(const std::string& str) {
            std::string res = str;
            std::transform(res.begin(), res.end(), res.begin(), [](unsigned char c) { return std::tolower(c); });
            return res;
        }
    };

    DreamToml() = default;

    bool LoadFile(const std::string& filePath) {
        std::ifstream file(filePath, std::ios::binary);
        if (!file.is_open()) return false;

        std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        return ParseString(content);
    }

    bool ParseString(const std::string& content) {
        m_data.clear();
        std::stringstream ss(content);
        std::string line;
        std::string currentSection = "";

        while (std::getline(ss, line)) {
            // Strip comments starting with # outside quotes
            bool inQuotes = false;
            char quoteChar = 0;
            size_t commentPos = std::string::npos;
            for (size_t i = 0; i < line.size(); ++i) {
                if ((line[i] == '"' || line[i] == '\'') && (i == 0 || line[i - 1] != '\\')) {
                    if (!inQuotes) {
                        inQuotes = true;
                        quoteChar = line[i];
                    } else if (line[i] == quoteChar) {
                        inQuotes = false;
                    }
                } else if (line[i] == '#' && !inQuotes) {
                    commentPos = i;
                    break;
                }
            }
            if (commentPos != std::string::npos) {
                line = line.substr(0, commentPos);
            }

            line = Trim(line);
            if (line.empty()) continue;

            // Section header: [Section] or [Section.Sub]
            if (line.front() == '[' && line.back() == ']') {
                currentSection = ToLower(Trim(line.substr(1, line.size() - 2)));
                continue;
            }

            // Key = Value
            size_t eqPos = line.find('=');
            if (eqPos != std::string::npos) {
                std::string key = ToLower(Trim(line.substr(0, eqPos)));
                std::string val = Trim(line.substr(eqPos + 1));
                if (!key.empty()) {
                    m_data[currentSection][key] = Value{val};
                }
            }
        }
        return true;
    }

    bool HasSection(const std::string& section) const {
        return m_data.find(ToLower(Trim(section))) != m_data.end();
    }

    bool HasKey(const std::string& section, const std::string& key) const {
        std::string sec = ToLower(Trim(section));
        auto it = m_data.find(sec);
        if (it == m_data.end()) return false;
        return it->second.find(ToLower(Trim(key))) != it->second.end();
    }

    Value Get(const std::string& section, const std::string& key) const {
        std::string sec = ToLower(Trim(section));
        std::string k = ToLower(Trim(key));
        auto it = m_data.find(sec);
        if (it != m_data.end()) {
            auto itKey = it->second.find(k);
            if (itKey != it->second.end()) {
                return itKey->second;
            }
        }
        return Value{""};
    }

    std::string GetString(const std::string& section, const std::string& key, const std::string& def = "") const {
        Value v = Get(section, key);
        return v.raw.empty() ? def : v.AsString(def);
    }

    bool GetBool(const std::string& section, const std::string& key, bool def = false) const {
        Value v = Get(section, key);
        return v.raw.empty() ? def : v.AsBool(def);
    }

    int64_t GetInt(const std::string& section, const std::string& key, int64_t def = 0) const {
        Value v = Get(section, key);
        return v.raw.empty() ? def : v.AsInt(def);
    }

    double GetFloat(const std::string& section, const std::string& key, double def = 0.0) const {
        Value v = Get(section, key);
        return v.raw.empty() ? def : v.AsFloat(def);
    }

    std::vector<std::string> GetStringArray(const std::string& section, const std::string& key) const {
        Value v = Get(section, key);
        return v.AsStringArray();
    }

private:
    static std::string Trim(const std::string& str) {
        size_t first = str.find_first_not_of(" \t\r\n");
        if (first == std::string::npos) return "";
        size_t last = str.find_last_not_of(" \t\r\n");
        return str.substr(first, (last - first + 1));
    }

    static std::string ToLower(const std::string& str) {
        std::string res = str;
        std::transform(res.begin(), res.end(), res.begin(), [](unsigned char c) { return std::tolower(c); });
        return res;
    }

    // Section -> (Key -> Value)
    std::unordered_map<std::string, std::unordered_map<std::string, Value>> m_data;
};

} // namespace dreamkurumi
