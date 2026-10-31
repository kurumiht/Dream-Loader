#pragma once
#include <windows.h>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <iostream>
#include <mutex>
#include "DreamScanner.hpp"

namespace dreamkurumi {

class DreamCache {
public:
    static DreamCache& Instance() {
        static DreamCache s_instance;
        return s_instance;
    }

    // Initialize cache relative to current module or game folder
    bool Initialize(const std::string& cacheFilePath = "dreamkurumi_cache.ini") {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_cachePath = cacheFilePath;
        m_currentModuleBase = reinterpret_cast<uintptr_t>(GetModuleHandleA(NULL));
        m_currentGameBuild = ReadExecutableBuildTimestamp();
        m_currentImageSize = ReadExecutableSizeOfImage();

        return LoadCache();
    }

    uint32_t GetGameBuildTimestamp() const {
        return m_currentGameBuild ? m_currentGameBuild : ReadExecutableBuildTimestamp();
    }

    uintptr_t GetModuleBase() const {
        return m_currentModuleBase ? m_currentModuleBase : reinterpret_cast<uintptr_t>(GetModuleHandleA(NULL));
    }

    bool IsCacheUpToDate() const {
        return (m_cachedBuild != 0 && m_cachedBuild == m_currentGameBuild);
    }

    // Resolve an RVA by symbol name: returns Absolute Address (Base + RVA)
    void* Resolve(const std::string& symbolName, uintptr_t fallbackRva = 0) {
        std::lock_guard<std::mutex> lock(m_mutex);
        EnsureInitializedLocked();

        auto it = m_offsets.find(symbolName);
        if (it != m_offsets.end()) {
            return reinterpret_cast<void*>(m_currentModuleBase + it->second);
        }

        // If not found in cache, register fallback if provided
        if (fallbackRva != 0) {
            m_offsets[symbolName] = fallbackRva;
            m_dirty = true;
            return reinterpret_cast<void*>(m_currentModuleBase + fallbackRva);
        }

        return nullptr;
    }

    // Pattern Scan fallback (Vectorized AVX2 / SSE2)
    // Runs ONLY once when a symbol is missing from cache or cache was invalidated
    void* PatternScan(const std::string& patternStr) {
        uintptr_t modBase = m_currentModuleBase ? m_currentModuleBase : reinterpret_cast<uintptr_t>(GetModuleHandleA(NULL));
        if (!modBase) return nullptr;

        auto dos = reinterpret_cast<IMAGE_DOS_HEADER*>(modBase);
        if (dos->e_magic != IMAGE_DOS_SIGNATURE) return nullptr;

        auto nt = reinterpret_cast<IMAGE_NT_HEADERS*>(reinterpret_cast<uint8_t*>(modBase) + dos->e_lfanew);
        if (nt->Signature != IMAGE_NT_SIGNATURE) return nullptr;

        uint8_t* scanStart = reinterpret_cast<uint8_t*>(modBase + nt->OptionalHeader.BaseOfCode);
        size_t scanSize = nt->OptionalHeader.SizeOfCode;

        return DreamScanner::Scan(scanStart, scanSize, patternStr);
    }

    // Resolve symbol using Cache first; if missing, scans pattern once and saves result to cache!
    void* ResolvePattern(const std::string& symbolName, const std::string& patternStr, uintptr_t fallbackRva = 0) {
        std::lock_guard<std::mutex> lock(m_mutex);
        EnsureInitializedLocked();

        // 1. Try cache first (O(1) instant 0ms lookup!)
        auto it = m_offsets.find(symbolName);
        if (it != m_offsets.end()) {
            return reinterpret_cast<void*>(m_currentModuleBase + it->second);
        }

        // 2. Cache miss: Scan pattern once!
        void* found = PatternScan(patternStr);
        if (found) {
            uintptr_t rva = reinterpret_cast<uintptr_t>(found) - m_currentModuleBase;
            m_offsets[symbolName] = rva;
            m_dirty = true;
            SaveCacheInternal();
            return found;
        }

        // 3. Fallback RVA
        if (fallbackRva != 0) {
            m_offsets[symbolName] = fallbackRva;
            m_dirty = true;
            SaveCacheInternal();
            return reinterpret_cast<void*>(m_currentModuleBase + fallbackRva);
        }

        return nullptr;
    }

    uintptr_t GetRva(const std::string& symbolName) {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_offsets.find(symbolName);
        if (it != m_offsets.end()) {
            return it->second;
        }
        return 0;
    }

    void SetOffset(const std::string& symbolName, uintptr_t rva) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_offsets[symbolName] = rva;
        m_dirty = true;
    }

    void Store(const std::string& symbolName, uintptr_t rva) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_offsets[symbolName] = rva;
        m_dirty = true;
        SaveCacheInternal();
    }

    bool SaveCache() {
        std::lock_guard<std::mutex> lock(m_mutex);
        return SaveCacheInternal();
    }

private:
    DreamCache() = default;

    void EnsureInitializedLocked() {
        if (m_currentModuleBase == 0) {
            m_currentModuleBase = reinterpret_cast<uintptr_t>(GetModuleHandleA(NULL));
            m_currentGameBuild = ReadExecutableBuildTimestamp();
            m_currentImageSize = ReadExecutableSizeOfImage();
            LoadCache();
        }
    }

    bool SaveCacheInternal() {
        std::ofstream file(m_cachePath);
        if (!file.is_open()) return false;

        file << "# dreamkurumiHT Symbol Offset Cache\n";
        file << "build_timestamp=" << m_currentGameBuild << "\n";
        file << "image_size=" << m_currentImageSize << "\n";
        file << "\n[offsets]\n";

        for (const auto& [name, rva] : m_offsets) {
            char hexBuf[32];
            snprintf(hexBuf, sizeof(hexBuf), "0x%llX", static_cast<unsigned long long>(rva));
            file << name << "=" << hexBuf << "\n";
        }

        m_cachedBuild = m_currentGameBuild;
        m_cachedImageSize = m_currentImageSize;
        m_dirty = false;
        return true;
    }

    static uint32_t ReadExecutableBuildTimestamp() {
        HMODULE hExe = GetModuleHandleA(NULL);
        if (!hExe) return 0;

        auto dos = reinterpret_cast<IMAGE_DOS_HEADER*>(hExe);
        if (dos->e_magic != IMAGE_DOS_SIGNATURE) return 0;

        auto nt = reinterpret_cast<IMAGE_NT_HEADERS*>(reinterpret_cast<uint8_t*>(hExe) + dos->e_lfanew);
        if (nt->Signature != IMAGE_NT_SIGNATURE) return 0;

        return nt->FileHeader.TimeDateStamp;
    }

    static uint32_t ReadExecutableSizeOfImage() {
        HMODULE hExe = GetModuleHandleA(NULL);
        if (!hExe) return 0;

        auto dos = reinterpret_cast<IMAGE_DOS_HEADER*>(hExe);
        if (dos->e_magic != IMAGE_DOS_SIGNATURE) return 0;

        auto nt = reinterpret_cast<IMAGE_NT_HEADERS*>(reinterpret_cast<uint8_t*>(hExe) + dos->e_lfanew);
        if (nt->Signature != IMAGE_NT_SIGNATURE) return 0;

        return nt->OptionalHeader.SizeOfImage;
    }

    bool LoadCache() {
        if (!std::filesystem::exists(m_cachePath)) {
            return false;
        }

        std::ifstream file(m_cachePath);
        if (!file.is_open()) return false;

        std::string line;
        bool inOffsetsSection = false;

        while (std::getline(file, line)) {
            line.erase(0, line.find_first_not_of(" \t\r\n"));
            if (line.empty() || line[0] == '#' || line[0] == ';') continue;

            if (line == "[offsets]") {
                inOffsetsSection = true;
                continue;
            }

            auto eq = line.find('=');
            if (eq == std::string::npos) continue;

            std::string key = line.substr(0, eq);
            std::string val = line.substr(eq + 1);

            key.erase(key.find_last_not_of(" \t\r\n") + 1);
            val.erase(0, val.find_first_not_of(" \t\r\n"));
            val.erase(val.find_last_not_of(" \t\r\n") + 1);

            if (key == "build_timestamp") {
                m_cachedBuild = static_cast<uint32_t>(std::strtoul(val.c_str(), nullptr, 0));
            } else if (key == "image_size") {
                m_cachedImageSize = static_cast<uint32_t>(std::strtoul(val.c_str(), nullptr, 0));
            } else if (inOffsetsSection || key.find("0x") != std::string::npos || val.find("0x") != std::string::npos) {
                uintptr_t rva = static_cast<uintptr_t>(std::strtoull(val.c_str(), nullptr, 0));
                m_offsets[key] = rva;
            }
        }

        bool buildMismatch = (m_cachedBuild != m_currentGameBuild);
        bool sizeMismatch = (m_cachedImageSize != 0 && m_currentImageSize != 0 && m_cachedImageSize != m_currentImageSize);

        if (buildMismatch || sizeMismatch) {
            DREAM_LOG_WARN("DreamCache: Game executable update or unverified cache detected! (Cached: 0x%08X/0x%X vs Executable: 0x%08X/0x%X). Invalidating stale offset cache.",
                           m_cachedBuild, m_cachedImageSize, m_currentGameBuild, m_currentImageSize);
            m_offsets.clear();
            m_dirty = true;
            return false;
        }

        return true;
    }

    std::string m_cachePath{"dreamkurumi_cache.ini"};
    uintptr_t m_currentModuleBase{0};
    uint32_t m_currentGameBuild{0};
    uint32_t m_cachedBuild{0};
    uint32_t m_currentImageSize{0};
    uint32_t m_cachedImageSize{0};
    std::unordered_map<std::string, uintptr_t> m_offsets;
    bool m_dirty{false};
    std::mutex m_mutex;
};

} // namespace dreamkurumi
