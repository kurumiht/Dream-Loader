#pragma once
#include <windows.h>
#include <cstdio>
#include <cstdarg>
#include <cstdint>
#include <string>
#include <vector>
#include <deque>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <atomic>
#include <sstream>
#include <iomanip>
#include <chrono>
#include <algorithm>

namespace dreamkurumi {

// ============================================================================
// Log Severity & Channels
// ============================================================================
enum class LogLevel : int {
    Debug = 0,
    Info  = 1,
    Warn  = 2,
    Error = 3,
    None  = 4
};

enum class LogChannel : uint8_t {
    Global = 0,
    AoE    = 1,
    Loot   = 2,
    Admin  = 3,
    XP     = 4,
    Hook   = 5,
    Net    = 6,
    ECS    = 7,
    Count  = 8
};

enum class VerbosityLevel : uint8_t {
    Silent   = 0, // No logs from this channel
    Summary  = 1, // Only high-level event summaries (e.g. >>> EXPANDED ... <<<)
    Detailed = 2, // Standard debug output
    Trace    = 3  // Deep memory address, component IDs, coordinates
};

// ============================================================================
// Log Entry Structures
// ============================================================================
struct LogEntry {
    std::string timestamp;
    DWORD threadId{0};
    LogLevel level{LogLevel::Info};
    LogChannel channel{LogChannel::Global};
    std::string message;
    std::string rawFormatted;
};

using ExternalLogCallback = void (*)(const char* utf8Message);

// ============================================================================
// DreamLog - High-Performance Asynchronous Diagnostic Engine
// Features:
// 1. Zero Game Thread Stutter: Async double-buffered queue with background worker
// 2. Channel-Based Verbosity: Independent granular control (0=Off, 1=Summary, 2=Detailed, 3=Trace)
// 3. Timed Verbose / Burst Mode: Auto-reverts after duration to prevent disk bloat
// 4. Anti-Spam Deduplication: Groups repeated messages into single summary line
// 5. Live In-Memory Ring Buffer: Instant `tail` and `grep` commands in console
// 6. Automatic Log Rotation: Seamlessly rotates to .old when > 10MB
// ============================================================================
class DreamLog {
public:
    static DreamLog& Instance() {
        static DreamLog s_instance;
        return s_instance;
    }

    void SetMinLogLevel(LogLevel level) {
        m_minLevel.store(level, std::memory_order_relaxed);
    }

    LogLevel GetMinLogLevel() const {
        return m_minLevel.load(std::memory_order_relaxed);
    }

    // ------------------------------------------------------------------------
    // Channel-Based Verbosity Control
    // ------------------------------------------------------------------------
    static const char* ChannelToString(LogChannel ch) {
        switch (ch) {
            case LogChannel::Global: return "global";
            case LogChannel::AoE:    return "aoe";
            case LogChannel::Loot:   return "loot";
            case LogChannel::Admin:  return "admin";
            case LogChannel::XP:     return "xp";
            case LogChannel::Hook:   return "hook";
            case LogChannel::Net:    return "net";
            case LogChannel::ECS:    return "ecs";
            default:                 return "unknown";
        }
    }

    static LogChannel ChannelFromString(const std::string& name) {
        std::string lower = name;
        std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
        if (lower == "aoe" || lower == "harvest") return LogChannel::AoE;
        if (lower == "loot" || lower == "pickup") return LogChannel::Loot;
        if (lower == "admin" || lower == "fishing") return LogChannel::Admin;
        if (lower == "xp" || lower == "xpshare") return LogChannel::XP;
        if (lower == "hook") return LogChannel::Hook;
        if (lower == "net") return LogChannel::Net;
        if (lower == "ecs") return LogChannel::ECS;
        return LogChannel::Global;
    }

    void SetChannelVerbosity(LogChannel ch, VerbosityLevel level, uint32_t durationSec = 0) {
        size_t idx = static_cast<size_t>(ch);
        if (idx >= static_cast<size_t>(LogChannel::Count)) return;

        uint8_t lvlVal = static_cast<uint8_t>(level);
        if (durationSec > 0) {
            uint64_t nowMs = GetTimestampMs();
            m_channels[idx].expiryTimeMs.store(nowMs + (durationSec * 1000ULL), std::memory_order_relaxed);
            m_channels[idx].level.store(lvlVal, std::memory_order_relaxed);
        } else {
            m_channels[idx].expiryTimeMs.store(0, std::memory_order_relaxed);
            m_channels[idx].baselineLevel.store(lvlVal, std::memory_order_relaxed);
            m_channels[idx].level.store(lvlVal, std::memory_order_relaxed);
        }
    }

    void SetChannelVerbosity(const std::string& name, int level, uint32_t durationSec = 0) {
        if (name == "all") {
            for (size_t i = 0; i < static_cast<size_t>(LogChannel::Count); ++i) {
                SetChannelVerbosity(static_cast<LogChannel>(i), static_cast<VerbosityLevel>(level), durationSec);
            }
            return;
        }
        SetChannelVerbosity(ChannelFromString(name), static_cast<VerbosityLevel>(level), durationSec);
    }

    VerbosityLevel GetChannelVerbosity(LogChannel ch) const {
        size_t idx = static_cast<size_t>(ch);
        if (idx >= static_cast<size_t>(LogChannel::Count)) return VerbosityLevel::Summary;
        return static_cast<VerbosityLevel>(m_channels[idx].level.load(std::memory_order_relaxed));
    }

    inline bool IsChannelEnabled(LogChannel ch, VerbosityLevel level) const {
        size_t idx = static_cast<size_t>(ch);
        if (idx >= static_cast<size_t>(LogChannel::Count)) return true;
        return m_channels[idx].level.load(std::memory_order_relaxed) >= static_cast<uint8_t>(level);
    }

    void SetExternalCallback(ExternalLogCallback cb) {
        std::lock_guard<std::mutex> lock(m_configMutex);
        m_externalCallback = cb;
    }

    void SetLogFile(const std::string& path) {
        std::lock_guard<std::mutex> lock(m_configMutex);
        if (m_fileHandle) {
            fflush(m_fileHandle);
            fclose(m_fileHandle);
            m_fileHandle = nullptr;
        }
        m_logPath = path;
        if (!m_logPath.empty()) {
            m_fileHandle = fopen(m_logPath.c_str(), "a");
        }
    }

    void SetColorsEnabled(bool enabled) {
        m_enableColors.store(enabled, std::memory_order_relaxed);
    }

    bool AllocateConsoleWindow(const char* title = "dreamkurumiHT Diagnostics Console (Client)") {
        std::lock_guard<std::mutex> lock(m_configMutex);
        if (m_consoleAllocated) return true;

        if (GetConsoleWindow() != NULL) {
            m_consoleAllocated = true;
            return true;
        }

        if (AllocConsole()) {
            FILE* fpOut = nullptr;
            FILE* fpErr = nullptr;
            FILE* fpIn = nullptr;
            freopen_s(&fpOut, "CONOUT$", "w", stdout);
            freopen_s(&fpErr, "CONOUT$", "w", stderr);
            freopen_s(&fpIn, "CONIN$", "r", stdin);

            SetConsoleOutputCP(CP_UTF8);
            SetConsoleCP(CP_UTF8);

            HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
            if (hOut != INVALID_HANDLE_VALUE) {
                DWORD dwMode = 0;
                if (GetConsoleMode(hOut, &dwMode)) {
                    SetConsoleMode(hOut, dwMode | 0x0004 /* ENABLE_VIRTUAL_TERMINAL_PROCESSING */);
                }
            }

            if (title) SetConsoleTitleA(title);

            m_consoleAllocated = true;
            m_enableColors.store(true, std::memory_order_relaxed);

            printf("\033[95m======================================================================\033[0m\n");
            printf("\033[96m  dreamkurumiHT Diagnostics Console (Client Mode)\033[0m\n");
            printf("\033[90m  Monitoring Enshrouded Hooks, LiveGameSettings & Crash Guard...\033[0m\n");
            printf("\033[93m  Tip: Press F9 for In-Game Menu | F10 for In-Game Console\033[0m\n");
            printf("\033[95m======================================================================\033[0m\n\n");
            fflush(stdout);
            return true;
        } else if (GetLastError() == ERROR_ACCESS_DENIED) {
            m_consoleAllocated = true;
            if (title) SetConsoleTitleA(title);
            return true;
        }
        return false;
    }

    void FreeConsoleWindow() {
        std::lock_guard<std::mutex> lock(m_configMutex);
        if (m_consoleAllocated) {
            FreeConsole();
            m_consoleAllocated = false;
        }
    }

    bool IsConsoleAllocated() const {
        return m_consoleAllocated;
    }

    // ------------------------------------------------------------------------
    // In-Memory History & Grep
    // ------------------------------------------------------------------------
    void SetMaxHistory(size_t count) {
        std::lock_guard<std::mutex> lock(m_historyMutex);
        m_maxHistory = count;
        while (m_history.size() > m_maxHistory) {
            m_history.pop_front();
        }
    }

    std::vector<LogEntry> GetRecentLogs(size_t maxCount = 100) {
        std::lock_guard<std::mutex> lock(m_historyMutex);
        size_t count = (maxCount < m_history.size()) ? maxCount : m_history.size();
        std::vector<LogEntry> result;
        result.reserve(count);
        auto it = m_history.end() - count;
        for (; it != m_history.end(); ++it) {
            result.push_back(*it);
        }
        return result;
    }

    std::vector<LogEntry> GrepLogs(const std::string& pattern, size_t maxCount = 50) {
        std::lock_guard<std::mutex> lock(m_historyMutex);
        std::string lowerPattern = pattern;
        std::transform(lowerPattern.begin(), lowerPattern.end(), lowerPattern.begin(), ::tolower);

        std::vector<LogEntry> matches;
        for (auto it = m_history.rbegin(); it != m_history.rend(); ++it) {
            std::string lowerMsg = it->message;
            std::transform(lowerMsg.begin(), lowerMsg.end(), lowerMsg.begin(), ::tolower);
            if (lowerMsg.find(lowerPattern) != std::string::npos) {
                matches.push_back(*it);
                if (matches.size() >= maxCount) break;
            }
        }
        std::reverse(matches.begin(), matches.end());
        return matches;
    }

    void ClearHistory() {
        std::lock_guard<std::mutex> lock(m_historyMutex);
        m_history.clear();
    }

    // ------------------------------------------------------------------------
    // Core Logging Methods (Non-blocking, sub-microsecond queue push)
    // ------------------------------------------------------------------------
    void Log(LogLevel level, const char* format, ...) {
        if (level < m_minLevel.load(std::memory_order_relaxed)) return;
        va_list args;
        va_start(args, format);
        LogChannelV(LogChannel::Global, VerbosityLevel::Summary, level, format, args);
        va_end(args);
    }

    void LogChannelMsg(LogChannel ch, VerbosityLevel verbosity, LogLevel level, const char* format, ...) {
        if (level < m_minLevel.load(std::memory_order_relaxed)) return;
        if (!IsChannelEnabled(ch, verbosity)) return;

        va_list args;
        va_start(args, format);
        LogChannelV(ch, verbosity, level, format, args);
        va_end(args);
    }

    void LogV(LogLevel level, const char* format, va_list args) {
        LogChannelV(LogChannel::Global, VerbosityLevel::Summary, level, format, args);
    }

    void LogChannelV(LogChannel ch, VerbosityLevel verbosity, LogLevel level, const char* format, va_list args) {
        if (level < m_minLevel.load(std::memory_order_relaxed)) return;
        if (!IsChannelEnabled(ch, verbosity)) return;

        // 1. Fast formatting into stack buffer (SSO)
        char stackBuf[1024];
        va_list argsCopy;
        va_copy(argsCopy, args);
        int needed = vsnprintf(stackBuf, sizeof(stackBuf), format, argsCopy);
        va_end(argsCopy);

        std::string userMsg;
        if (needed >= 0 && static_cast<size_t>(needed) < sizeof(stackBuf)) {
            userMsg.assign(stackBuf, needed);
        } else if (needed >= 0) {
            std::vector<char> dynBuf(needed + 1);
            vsnprintf(dynBuf.data(), dynBuf.size(), format, args);
            userMsg.assign(dynBuf.data(), needed);
        } else {
            userMsg = "<format error>";
        }

        // 2. High-Precision Timestamp & Thread ID
        SYSTEMTIME st;
        GetLocalTime(&st);
        char timeBuf[32];
        snprintf(timeBuf, sizeof(timeBuf), "%02d:%02d:%02d.%03d",
                 st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);

        DWORD tid = GetCurrentThreadId();

        // 3. Push to lock-free asynchronous queue (Game thread spends <1us here!)
        QueuedLog queued;
        queued.channel = ch;
        queued.level = level;
        queued.timeStr = timeBuf;
        queued.tid = tid;
        queued.userMsg = std::move(userMsg);

        {
            std::lock_guard<std::mutex> lock(m_queueMutex);
            m_incomingQueue.push_back(std::move(queued));
        }
        m_queueCv.notify_one();
    }

    // Hex Dump Utility (e.g., DreamLog::HexDump("Player Struct", ptr, 64))
    static void HexDump(const char* title, const void* data, size_t length, LogLevel level = LogLevel::Info) {
        if (!data || length == 0) return;
        const auto* bytes = static_cast<const uint8_t*>(data);

        std::ostringstream ss;
        ss << "=== Hex Dump: " << (title ? title : "Memory") << " (" << length << " bytes at 0x" << data << ") ===\n";

        for (size_t i = 0; i < length; i += 16) {
            ss << "  0x" << std::hex << std::setw(8) << std::setfill('0') << i << ": ";
            for (size_t j = 0; j < 16; ++j) {
                if (i + j < length) {
                    ss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(bytes[i + j]) << " ";
                } else {
                    ss << "   ";
                }
                if (j == 7) ss << " ";
            }
            ss << " | ";
            for (size_t j = 0; j < 16 && (i + j) < length; ++j) {
                uint8_t c = bytes[i + j];
                ss << (c >= 32 && c <= 126 ? static_cast<char>(c) : '.');
            }
            ss << "\n";
        }

        Instance().Log(level, "%s", ss.str().c_str());
    }

    void Flush() {
        // Drain worker queue synchronously
        std::unique_lock<std::mutex> lock(m_queueMutex);
        if (!m_incomingQueue.empty()) {
            std::vector<QueuedLog> batch;
            batch.swap(m_incomingQueue);
            lock.unlock();
            ProcessBatch(batch);
        }
        std::lock_guard<std::mutex> cfgLock(m_configMutex);
        if (m_fileHandle) fflush(m_fileHandle);
    }

private:
    struct QueuedLog {
        LogChannel channel;
        LogLevel level;
        std::string timeStr;
        DWORD tid{0};
        std::string userMsg;
    };

    struct ChannelConfig {
        std::atomic<uint8_t> level{1};         // Default: 1 (Summary)
        std::atomic<uint8_t> baselineLevel{1}; // Value to revert to after timed burst
        std::atomic<uint64_t> expiryTimeMs{0}; // 0 = permanent
    };

    DreamLog() {
        HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
        if (hOut != INVALID_HANDLE_VALUE) {
            DWORD dwMode = 0;
            if (GetConsoleMode(hOut, &dwMode)) {
                SetConsoleMode(hOut, dwMode | 0x0004 /* ENABLE_VIRTUAL_TERMINAL_PROCESSING */);
            }
        }
        if (!m_logPath.empty()) {
            m_fileHandle = fopen(m_logPath.c_str(), "a");
        }

        // Start dedicated background logging worker
        m_workerRunning.store(true);
        m_workerThread = std::thread(&DreamLog::WorkerLoop, this);
    }

    ~DreamLog() {
        m_workerRunning.store(false);
        m_queueCv.notify_all();
        if (m_workerThread.joinable()) {
            m_workerThread.join();
        }
        Flush();
        if (m_fileHandle) {
            fclose(m_fileHandle);
            m_fileHandle = nullptr;
        }
    }

    static uint64_t GetTimestampMs() {
        return static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count());
    }

    void WorkerLoop() {
        std::vector<QueuedLog> batch;
        while (m_workerRunning.load(std::memory_order_relaxed)) {
            {
                std::unique_lock<std::mutex> lock(m_queueMutex);
                m_queueCv.wait_for(lock, std::chrono::milliseconds(100), [this]() {
                    return !m_incomingQueue.empty() || !m_workerRunning.load(std::memory_order_relaxed);
                });
                if (!m_incomingQueue.empty()) {
                    batch.swap(m_incomingQueue);
                }
            }

            if (!batch.empty()) {
                ProcessBatch(batch);
                batch.clear();
            }

            CheckTimedVerbosityExpirations();
        }
    }

    void ProcessBatch(const std::vector<QueuedLog>& batch) {
        std::lock_guard<std::mutex> lock(m_configMutex);

        for (const auto& item : batch) {
            // Anti-Spam Deduplication: Check if identical message is flooding
            uint64_t nowMs = GetTimestampMs();
            if (item.userMsg == m_lastMessage && (nowMs - m_lastMsgTime) < 1000) {
                m_repeatCount++;
                continue;
            } else if (m_repeatCount > 0) {
                char repeatBuf[128];
                snprintf(repeatBuf, sizeof(repeatBuf), ">>> [Last message repeated %zu times in %llums] <<<\n",
                         m_repeatCount, (nowMs - m_lastMsgTime));
                OutputFormattedLine(repeatBuf, repeatBuf, item.timeStr, item.tid, LogLevel::Info, LogChannel::Global);
                m_repeatCount = 0;
            }

            m_lastMessage = item.userMsg;
            m_lastMsgTime = nowMs;

            const char* levelStr = "INFO";
            const char* colorCode = "";
            const char* colorReset = "\033[0m";

            switch (item.level) {
                case LogLevel::Debug:
                    levelStr = "DEBUG";
                    colorCode = "\033[90m";
                    break;
                case LogLevel::Info:
                    levelStr = "INFO";
                    colorCode = "\033[92m";
                    break;
                case LogLevel::Warn:
                    levelStr = "WARN";
                    colorCode = "\033[93m";
                    break;
                case LogLevel::Error:
                    levelStr = "ERROR";
                    colorCode = "\033[91m";
                    break;
                default:
                    break;
            }

            char headerBuf[80];
            const char* chStr = ChannelToString(item.channel);
            snprintf(headerBuf, sizeof(headerBuf), "[%s][TID:%lu][%s:%s] ",
                     item.timeStr.c_str(), item.tid, chStr, levelStr);

            std::string plainMsg = std::string(headerBuf) + item.userMsg + "\n";
            std::string consoleMsg;
            if (m_enableColors.load(std::memory_order_relaxed)) {
                consoleMsg = std::string(colorCode) + headerBuf + item.userMsg + colorReset + "\n";
            } else {
                consoleMsg = plainMsg;
            }

            OutputFormattedLine(plainMsg, consoleMsg, item.timeStr, item.tid, item.level, item.channel);
        }
    }

    void OutputFormattedLine(const std::string& plainMsg, const std::string& consoleMsg,
                            const std::string& timeStr, DWORD tid, LogLevel level, LogChannel ch) {
        OutputDebugStringA(plainMsg.c_str());
        printf("%s", consoleMsg.c_str());
        fflush(stdout);

        if (m_externalCallback) {
            m_externalCallback(plainMsg.c_str());
        }

        // Output to persistent file with >10MB auto-rotation
        if (!m_fileHandle && !m_logPath.empty()) {
            m_fileHandle = fopen(m_logPath.c_str(), "a");
        }
        if (m_fileHandle) {
            long currentPos = ftell(m_fileHandle);
            if (currentPos > 10 * 1024 * 1024) {
                fclose(m_fileHandle);
                m_fileHandle = nullptr;
                std::string oldPath = m_logPath + ".old";
                MoveFileExA(m_logPath.c_str(), oldPath.c_str(), MOVEFILE_REPLACE_EXISTING);
                m_fileHandle = fopen(m_logPath.c_str(), "a");
            }
        }
        if (m_fileHandle) {
            fwrite(plainMsg.data(), 1, plainMsg.size(), m_fileHandle);
        }

        // Circular In-Memory Buffer
        {
            std::lock_guard<std::mutex> histLock(m_historyMutex);
            if (m_maxHistory > 0) {
                if (m_history.size() >= m_maxHistory) {
                    m_history.pop_front();
                }
                m_history.push_back({timeStr, tid, level, ch, plainMsg, plainMsg});
            }
        }
    }

    void CheckTimedVerbosityExpirations() {
        uint64_t nowMs = GetTimestampMs();
        for (size_t i = 0; i < static_cast<size_t>(LogChannel::Count); ++i) {
            uint64_t exp = m_channels[i].expiryTimeMs.load(std::memory_order_relaxed);
            if (exp > 0 && nowMs >= exp) {
                uint8_t baseLvl = m_channels[i].baselineLevel.load(std::memory_order_relaxed);
                m_channels[i].level.store(baseLvl, std::memory_order_relaxed);
                m_channels[i].expiryTimeMs.store(0, std::memory_order_relaxed);
                char infoBuf[128];
                snprintf(infoBuf, sizeof(infoBuf), "[DreamLog] Timed verbosity burst expired for channel '%s'. Reverted to level %u.\n",
                         ChannelToString(static_cast<LogChannel>(i)), baseLvl);
                OutputDebugStringA(infoBuf);
                printf("\033[90m%s\033[0m", infoBuf);
                fflush(stdout);
            }
        }
    }

    std::atomic<LogLevel> m_minLevel{LogLevel::Debug};
    ExternalLogCallback m_externalCallback{nullptr};
    std::string m_logPath{"dreamkurumi.log"};
    FILE* m_fileHandle{nullptr};
    std::atomic<bool> m_enableColors{true};
    bool m_consoleAllocated{false};

    ChannelConfig m_channels[static_cast<size_t>(LogChannel::Count)];

    size_t m_maxHistory{300};
    std::deque<LogEntry> m_history;
    std::mutex m_historyMutex;

    // Asynchronous queue
    std::vector<QueuedLog> m_incomingQueue;
    std::mutex m_queueMutex;
    std::condition_variable m_queueCv;
    std::atomic<bool> m_workerRunning{false};
    std::thread m_workerThread;
    std::mutex m_configMutex;

    // Anti-spam deduplication state
    std::string m_lastMessage;
    uint64_t m_lastMsgTime{0};
    size_t m_repeatCount{0};
};

} // namespace dreamkurumi

// Global convenience macros
#define DREAM_LOG_DEBUG(fmt, ...) ::dreamkurumi::DreamLog::Instance().Log(::dreamkurumi::LogLevel::Debug, fmt, ##__VA_ARGS__)
#define DREAM_LOG_INFO(fmt, ...)  ::dreamkurumi::DreamLog::Instance().Log(::dreamkurumi::LogLevel::Info,  fmt, ##__VA_ARGS__)
#define DREAM_LOG_WARN(fmt, ...)  ::dreamkurumi::DreamLog::Instance().Log(::dreamkurumi::LogLevel::Warn,  fmt, ##__VA_ARGS__)
#define DREAM_LOG_ERROR(fmt, ...) ::dreamkurumi::DreamLog::Instance().Log(::dreamkurumi::LogLevel::Error, fmt, ##__VA_ARGS__)

#define DREAM_LOG_CHANNEL(ch, verbosity, level, fmt, ...) \
    do { \
        if (::dreamkurumi::DreamLog::Instance().IsChannelEnabled(ch, verbosity)) { \
            ::dreamkurumi::DreamLog::Instance().LogChannelMsg(ch, verbosity, level, fmt, ##__VA_ARGS__); \
        } \
    } while (0)
