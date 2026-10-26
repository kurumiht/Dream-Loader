#pragma once
#include <windows.h>
#include <string>
#include <vector>
#include <chrono>
#include <thread>
#include <atomic>
#include <mutex>
#include <filesystem>
#include <algorithm>
#include <cstdio>
#include "DreamLog.hpp"

namespace dreamkurumi {

// ============================================================================
// DreamBackup: World Save Auto-Backup and Snapshot System
// Provides safe, zero-downtime hot backups of Enshrouded savegames (*.ksd, *.json)
// ============================================================================
class DreamBackup {
public:
    struct BackupResult {
        bool success{false};
        std::string backupDirectory;
        std::vector<std::string> backedUpFiles;
        uint64_t totalBytes{0};
        std::string message;
    };

    struct BackupFileInfo {
        std::string filename;
        std::string fullPath;
        uint64_t fileSize{0};
        FILETIME lastWriteTime{0, 0};
    };

    static DreamBackup& Instance() {
        static DreamBackup s_instance;
        return s_instance;
    }

    // Locate Enshrouded savegame folder
    std::filesystem::path FindSaveDirectory() const {
        std::vector<std::filesystem::path> candidates = {
            "savegame",
            "../savegame",
            "../../savegame",
            "savegame - Copy"
        };

        for (const auto& p : candidates) {
            std::error_code ec;
            if (std::filesystem::exists(p, ec) && std::filesystem::is_directory(p, ec)) {
                // Check if it contains .ksd files
                for (const auto& entry : std::filesystem::directory_iterator(p, ec)) {
                    if (entry.is_regular_file() && entry.path().extension() == ".ksd") {
                        return std::filesystem::absolute(p);
                    }
                }
                // Return directory even if empty
                return std::filesystem::absolute(p);
            }
        }
        return "savegame";
    }

    // Perform an immediate hot backup
    BackupResult CreateBackup(const std::string& tag = "") {
        std::lock_guard<std::mutex> lock(m_mutex);
        BackupResult result;

        std::filesystem::path saveDir = FindSaveDirectory();
        std::error_code ec;
        if (!std::filesystem::exists(saveDir, ec)) {
            result.success = false;
            result.message = "Savegame directory not found: " + saveDir.string();
            return result;
        }

        std::filesystem::path backupRoot = "save_backups";
        std::filesystem::create_directories(backupRoot, ec);
        result.backupDirectory = std::filesystem::absolute(backupRoot).string();

        SYSTEMTIME st;
        GetLocalTime(&st);
        char timeStamp[64];
        snprintf(timeStamp, sizeof(timeStamp), "%04d%02d%02d_%02d%02d%02d",
                 st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);

        int count = 0;
        uint64_t bytes = 0;

        for (const auto& entry : std::filesystem::directory_iterator(saveDir, ec)) {
            if (entry.is_regular_file()) {
                std::string ext = entry.path().extension().string();
                std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

                // Backup .ksd savegame files and json configurations
                if (ext == ".ksd" || ext == ".json") {
                    std::string stem = entry.path().stem().string();
                    std::string outFilename = stem + "_" + timeStamp;
                    if (!tag.empty()) {
                        outFilename += "_" + tag;
                    }
                    outFilename += ext;

                    std::filesystem::path dst = backupRoot / outFilename;

                    // Copy file with OS-level copy (failsafe against file locks)
                    if (CopyFileW(entry.path().c_str(), dst.c_str(), FALSE)) {
                        uint64_t fSize = std::filesystem::file_size(dst, ec);
                        bytes += fSize;
                        result.backedUpFiles.push_back(outFilename);
                        count++;
                    }
                }
            }
        }

        if (count > 0) {
            result.success = true;
            result.totalBytes = bytes;
            char msg[256];
            snprintf(msg, sizeof(msg), "Successfully backed up %d file(s) (%.2f MB) to '%s'",
                     count, static_cast<double>(bytes) / (1024.0 * 1024.0), backupRoot.string().c_str());
            result.message = msg;
            DREAM_LOG_INFO("[DreamBackup] %s", msg);

            // Prune excess backups
            PruneBackups(m_maxBackupsToKeep);
        } else {
            result.success = false;
            result.message = "No .ksd or save files found in '" + saveDir.string() + "'";
        }

        return result;
    }

    // List all existing backups
    std::vector<BackupFileInfo> ListBackups() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        std::vector<BackupFileInfo> list;
        std::filesystem::path backupRoot = "save_backups";
        std::error_code ec;

        if (!std::filesystem::exists(backupRoot, ec)) return list;

        for (const auto& entry : std::filesystem::directory_iterator(backupRoot, ec)) {
            if (entry.is_regular_file()) {
                BackupFileInfo info;
                info.filename = entry.path().filename().string();
                info.fullPath = std::filesystem::absolute(entry.path()).string();
                info.fileSize = std::filesystem::file_size(entry.path(), ec);

                WIN32_FILE_ATTRIBUTE_DATA fad;
                if (GetFileAttributesExW(entry.path().c_str(), GetFileExInfoStandard, &fad)) {
                    info.lastWriteTime = fad.ftLastWriteTime;
                }
                list.push_back(info);
            }
        }

        // Sort newest first
        std::sort(list.begin(), list.end(), [](const BackupFileInfo& a, const BackupFileInfo& b) {
            return CompareFileTime(&a.lastWriteTime, &b.lastWriteTime) > 0;
        });

        return list;
    }

    // Retain only the most recent N backups
    void PruneBackups(size_t maxToKeep) {
        if (maxToKeep == 0) return;
        auto backups = ListBackups();
        if (backups.size() <= maxToKeep) return;

        std::error_code ec;
        for (size_t i = maxToKeep; i < backups.size(); ++i) {
            std::filesystem::remove(backups[i].fullPath, ec);
            DREAM_LOG_INFO("[DreamBackup] Pruned old backup: %s", backups[i].filename.c_str());
        }
    }

    // Configure and start background auto-backup scheduler
    void StartAutoBackup(bool enabled, uint32_t intervalMinutes, size_t maxBackups = 10) {
        m_maxBackupsToKeep = maxBackups;
        if (!enabled || intervalMinutes == 0) {
            StopAutoBackup();
            return;
        }

        if (m_autoRunning.exchange(true)) return;

        m_intervalMinutes = intervalMinutes;
        m_autoThread = std::thread([this]() {
            DREAM_LOG_INFO("[DreamBackup] Auto-backup background thread active (Every %u minutes, Max %zu archives)",
                           m_intervalMinutes, m_maxBackupsToKeep);

            while (m_autoRunning.load()) {
                // Sleep in small 1-second chunks for responsive cancellation
                for (uint32_t i = 0; i < m_intervalMinutes * 60 && m_autoRunning.load(); ++i) {
                    std::this_thread::sleep_for(std::chrono::seconds(1));
                }

                if (m_autoRunning.load()) {
                    DREAM_LOG_INFO("[DreamBackup] Triggering periodic auto-backup...");
                    CreateBackup("autobackup");
                }
            }
        });
    }

    void StopAutoBackup() {
        if (!m_autoRunning.exchange(false)) return;
        if (m_autoThread.joinable()) {
            m_autoThread.detach();
        }
        DREAM_LOG_INFO("[DreamBackup] Auto-backup thread stopped");
    }

    void SetMaxBackups(size_t maxBackups) {
        m_maxBackupsToKeep = maxBackups;
    }

private:
    DreamBackup() = default;
    ~DreamBackup() {
        StopAutoBackup();
    }

    mutable std::mutex m_mutex;
    std::atomic<bool> m_autoRunning{false};
    uint32_t m_intervalMinutes{30};
    size_t m_maxBackupsToKeep{10};
    std::thread m_autoThread;
};

} // namespace dreamkurumi
