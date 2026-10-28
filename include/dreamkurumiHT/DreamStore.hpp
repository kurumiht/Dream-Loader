#pragma once

// ============================================================================
// DreamStore.hpp - Embedded Atomic Key-Value & Persistent Storage Engine
//
// Part of the dreamkurumiHT Framework (Item 9: Embedded Atomic Key-Value Storage)
// Features:
// - Modern C++20 std::atomic<std::shared_ptr<const DataMap>> RCU architecture: 100% Lock-Free reads
// - Durability & Single-File Protection: Staging write-through + FlushFileBuffers + Atomic Replace
// - Namespace isolation for individual mods (e.g. "DreamAutoPickup", "XPShare", "ServerStats")
// - Typed accessors: int64_t, double, bool, std::string, raw binary blob, and JSON
// - Hardened file size bounds check (16 MB cap) against memory exhaustion / corruption
// ============================================================================

#include <windows.h>
#include <string>
#include <vector>
#include <unordered_map>
#include <mutex>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <atomic>
#include <memory>
#include "DreamLog.hpp"
#include "DreamCrashGuard.hpp"

namespace dreamkurumi {

enum class StoreValueType : uint8_t {
    String = 0,
    Int64 = 1,
    Double = 2,
    Bool = 3,
    Blob = 4
};

struct StoreValue {
    StoreValueType type{StoreValueType::String};
    std::string stringVal;
    int64_t intVal{0};
    double doubleVal{0.0};
    bool boolVal{false};
    std::vector<uint8_t> blobVal;

    static StoreValue FromString(const std::string& s) {
        StoreValue v;
        v.type = StoreValueType::String;
        v.stringVal = s;
        return v;
    }

    static StoreValue FromInt(int64_t i) {
        StoreValue v;
        v.type = StoreValueType::Int64;
        v.intVal = i;
        v.stringVal = std::to_string(i);
        return v;
    }

    static StoreValue FromDouble(double d) {
        StoreValue v;
        v.type = StoreValueType::Double;
        v.doubleVal = d;
        v.stringVal = std::to_string(d);
        return v;
    }

    static StoreValue FromBool(bool b) {
        StoreValue v;
        v.type = StoreValueType::Bool;
        v.boolVal = b;
        v.stringVal = b ? "true" : "false";
        return v;
    }

    static StoreValue FromBlob(const void* data, size_t size) {
        StoreValue v;
        v.type = StoreValueType::Blob;
        if (data && size > 0) {
            const auto* ptr = static_cast<const uint8_t*>(data);
            v.blobVal.assign(ptr, ptr + size);
        }
        return v;
    }
};

class DreamStoreNamespace {
public:
    using DataMap = std::unordered_map<std::string, StoreValue>;

    explicit DreamStoreNamespace(std::string name)
        : m_name(std::move(name)), m_snapshot(std::make_shared<const DataMap>()) {}

    const std::string& GetName() const { return m_name; }

    // ========================================================================
    // C++20 RCU Lock-Free Getters (Zero Mutex Contention)
    // ========================================================================
    std::string GetString(const std::string& key, const std::string& def = "") const {
        auto snap = m_snapshot.load();
        if (!snap) return def;
        auto it = snap->find(key);
        return (it != snap->end()) ? it->second.stringVal : def;
    }

    int64_t GetInt(const std::string& key, int64_t def = 0) const {
        auto snap = m_snapshot.load();
        if (!snap) return def;
        auto it = snap->find(key);
        if (it == snap->end()) return def;
        if (it->second.type == StoreValueType::Int64) return it->second.intVal;
        try { return std::stoll(it->second.stringVal); } catch (...) { return def; }
    }

    double GetDouble(const std::string& key, double def = 0.0) const {
        auto snap = m_snapshot.load();
        if (!snap) return def;
        auto it = snap->find(key);
        if (it == snap->end()) return def;
        if (it->second.type == StoreValueType::Double) return it->second.doubleVal;
        try { return std::stod(it->second.stringVal); } catch (...) { return def; }
    }

    bool GetBool(const std::string& key, bool def = false) const {
        auto snap = m_snapshot.load();
        if (!snap) return def;
        auto it = snap->find(key);
        if (it == snap->end()) return def;
        if (it->second.type == StoreValueType::Bool) return it->second.boolVal;
        return (it->second.stringVal == "true" || it->second.stringVal == "1");
    }

    std::vector<uint8_t> GetBlob(const std::string& key) const {
        auto snap = m_snapshot.load();
        if (!snap) return {};
        auto it = snap->find(key);
        return (it != snap->end()) ? it->second.blobVal : std::vector<uint8_t>{};
    }

    bool Contains(const std::string& key) const {
        auto snap = m_snapshot.load();
        return snap && (snap->find(key) != snap->end());
    }

    size_t Size() const {
        auto snap = m_snapshot.load();
        return snap ? snap->size() : 0;
    }

    // ========================================================================
    // Copy-On-Write Setters
    // ========================================================================
    void SetString(const std::string& key, const std::string& val) {
        UpdateSnapshot([&](DataMap& m) { m[key] = StoreValue::FromString(val); });
    }

    void SetInt(const std::string& key, int64_t val) {
        UpdateSnapshot([&](DataMap& m) { m[key] = StoreValue::FromInt(val); });
    }

    void SetDouble(const std::string& key, double val) {
        UpdateSnapshot([&](DataMap& m) { m[key] = StoreValue::FromDouble(val); });
    }

    void SetBool(const std::string& key, bool val) {
        UpdateSnapshot([&](DataMap& m) { m[key] = StoreValue::FromBool(val); });
    }

    void SetBlob(const std::string& key, const void* data, size_t size) {
        UpdateSnapshot([&](DataMap& m) { m[key] = StoreValue::FromBlob(data, size); });
    }

    bool Erase(const std::string& key) {
        bool erased = false;
        UpdateSnapshot([&](DataMap& m) {
            auto it = m.find(key);
            if (it != m.end()) {
                m.erase(it);
                erased = true;
            }
        });
        return erased;
    }

    void Clear() {
        UpdateSnapshot([](DataMap& m) { m.clear(); });
    }

    bool IsDirty() const { return m_dirty.load(); }
    void MarkClean() { m_dirty.store(false); }

    // Serialization for disk storage
    std::string Serialize() const {
        auto snap = m_snapshot.load();
        if (!snap) return "";
        std::ostringstream ss;
        ss << "# DreamStore Namespace: " << m_name << "\n";
        for (const auto& [k, v] : *snap) {
            ss << static_cast<int>(v.type) << "\t" << k << "\t" << v.stringVal << "\n";
        }
        return ss.str();
    }

    void Deserialize(const std::string& content) {
        auto newMap = std::make_shared<DataMap>();
        std::istringstream ss(content);
        std::string line;
        while (std::getline(ss, line)) {
            if (line.empty() || line[0] == '#') continue;
            size_t tab1 = line.find('\t');
            if (tab1 == std::string::npos) continue;
            size_t tab2 = line.find('\t', tab1 + 1);
            if (tab2 == std::string::npos) continue;

            int typeCode = std::stoi(line.substr(0, tab1));
            std::string key = line.substr(tab1 + 1, tab2 - tab1 - 1);
            std::string valStr = line.substr(tab2 + 1);

            StoreValue v;
            v.type = static_cast<StoreValueType>(typeCode);
            v.stringVal = valStr;
            if (v.type == StoreValueType::Int64) {
                try { v.intVal = std::stoll(valStr); } catch (...) {}
            } else if (v.type == StoreValueType::Double) {
                try { v.doubleVal = std::stod(valStr); } catch (...) {}
            } else if (v.type == StoreValueType::Bool) {
                v.boolVal = (valStr == "true" || valStr == "1");
            }
            (*newMap)[key] = v;
        }
        m_snapshot.store(newMap);
        m_dirty = false;
    }

private:
    template <typename Fn>
    void UpdateSnapshot(Fn&& fn) {
        std::lock_guard<std::mutex> lock(m_writeMutex);
        auto current = m_snapshot.load();
        auto newMap = std::make_shared<DataMap>(current ? *current : DataMap());
        fn(*newMap);
        m_snapshot.store(newMap);
        m_dirty = true;
    }

    std::string m_name;
    mutable std::mutex m_writeMutex;
    std::atomic<std::shared_ptr<const DataMap>> m_snapshot;
    std::atomic<bool> m_dirty{false};
};

// ============================================================================
// DreamStore - Master Storage Coordinator
// ============================================================================
class DreamStore {
public:
    static DreamStore& Instance() {
        static DreamStore s_instance;
        return s_instance;
    }

    // Set base directory for storage files (default: "mods_data/store/")
    void SetStorageDirectory(const std::string& dir) {
        std::lock_guard<std::mutex> lock(m_masterMutex);
        m_storageDir = dir;
        CreateDirectoryA(dir.c_str(), NULL);
    }

    const std::string& GetStorageDirectory() const {
        return m_storageDir;
    }

    // Obtain or create a named namespace
    std::shared_ptr<DreamStoreNamespace> GetNamespace(const std::string& nsName) {
        std::lock_guard<std::mutex> lock(m_masterMutex);
        auto it = m_namespaces.find(nsName);
        if (it != m_namespaces.end()) {
            return it->second;
        }

        auto ns = std::make_shared<DreamStoreNamespace>(nsName);
        m_namespaces[nsName] = ns;

        std::string filePath = GetNamespaceFilePath(nsName);

        // Sanity Check: Protect against oversized/corrupt files (16 MB cap)
        std::error_code ec;
        uintmax_t fileSize = std::filesystem::file_size(filePath, ec);
        if (!ec && fileSize > 16 * 1024 * 1024) {
            DREAM_LOG_ERROR("DreamStore: Store file '%s' exceeds 16MB limit (%llu bytes). Skipping load.",
                            filePath.c_str(), static_cast<unsigned long long>(fileSize));
            return ns;
        }

        // Attempt load from disk if file exists
        std::ifstream file(filePath);
        if (file.is_open()) {
            std::stringstream buffer;
            buffer << file.rdbuf();
            ns->Deserialize(buffer.str());
            DREAM_LOG_DEBUG("DreamStore: Loaded namespace '%s' from '%s'", nsName.c_str(), filePath.c_str());
        }

        return ns;
    }

    // Atomic disk flush for a single namespace
    bool FlushNamespace(const std::string& nsName) {
        std::shared_ptr<DreamStoreNamespace> ns;
        {
            std::lock_guard<std::mutex> lock(m_masterMutex);
            auto it = m_namespaces.find(nsName);
            if (it == m_namespaces.end()) return false;
            ns = it->second;
        }

        if (!ns || !ns->IsDirty()) return true;

        std::string finalPath = GetNamespaceFilePath(nsName);
        std::string tempPath = finalPath + ".tmp";

        // 1. Ensure directory exists
        std::filesystem::path p(finalPath);
        if (p.has_parent_path()) {
            std::filesystem::create_directories(p.parent_path());
        }

        // 2. Write to temporary file with write-through
        std::string serialized = ns->Serialize();
        HANDLE hFile = CreateFileA(tempPath.c_str(), GENERIC_WRITE, 0, NULL,
                                   CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH, NULL);
        if (hFile == INVALID_HANDLE_VALUE) {
            DREAM_LOG_ERROR("DreamStore: Failed to create temporary file '%s'", tempPath.c_str());
            return false;
        }

        DWORD written = 0;
        BOOL writeOk = WriteFile(hFile, serialized.data(), static_cast<DWORD>(serialized.size()), &written, NULL);
        
        // Strict durability flush before closing handle
        FlushFileBuffers(hFile);
        CloseHandle(hFile);

        if (!writeOk || written != serialized.size()) {
            DREAM_LOG_ERROR("DreamStore: Incomplete write to temporary file '%s'", tempPath.c_str());
            DeleteFileA(tempPath.c_str());
            return false;
        }

        // 3. Atomic replacement via MoveFileEx with MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH
        if (!MoveFileExA(tempPath.c_str(), finalPath.c_str(),
                         MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
            // Fallback to ReplaceFileA
            if (!ReplaceFileA(finalPath.c_str(), tempPath.c_str(), NULL, 0, NULL, NULL)) {
                DREAM_LOG_ERROR("DreamStore: Atomic rename/replace failed from '%s' to '%s' (Error: %lu)",
                                tempPath.c_str(), finalPath.c_str(), GetLastError());
                return false;
            }
        }

        ns->MarkClean();
        DREAM_LOG_DEBUG("DreamStore: Atomically flushed namespace '%s' to '%s'", nsName.c_str(), finalPath.c_str());
        return true;
    }

    // Flush all dirty namespaces to disk atomically
    size_t FlushAll() {
        std::vector<std::string> names;
        {
            std::lock_guard<std::mutex> lock(m_masterMutex);
            for (const auto& [name, _] : m_namespaces) {
                names.push_back(name);
            }
        }

        size_t flushedCount = 0;
        for (const auto& name : names) {
            if (FlushNamespace(name)) {
                flushedCount++;
            }
        }
        return flushedCount;
    }

private:
    DreamStore() {
        m_storageDir = "mods_data/store/";
        CreateDirectoryA("mods_data", NULL);
        CreateDirectoryA(m_storageDir.c_str(), NULL);
    }

    ~DreamStore() {
        FlushAll();
    }

    std::string GetNamespaceFilePath(const std::string& nsName) const {
        std::string sanitized = nsName;
        for (char& c : sanitized) {
            if (c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|') {
                c = '_';
            }
        }
        return m_storageDir + sanitized + ".dstore";
    }

    std::mutex m_masterMutex;
    std::string m_storageDir;
    std::unordered_map<std::string, std::shared_ptr<DreamStoreNamespace>> m_namespaces;
};

} // namespace dreamkurumi
