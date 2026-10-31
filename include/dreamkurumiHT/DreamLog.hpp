#pragma once
// ============================================================================
// DreamLog v2 - FPS-first asynchronous diagnostic logger
//
// Hot path (game thread) rules:
//   * Disabled log  -> 2 relaxed atomic loads, no function call, args NOT evaluated
//   * Enabled log   -> vsnprintf into a stack buffer + 1 CAS + memcpy into a
//                      preallocated ring slot + 1 QueryPerformanceCounter.
//   * NO mutex, NO heap allocation, NO syscall (except QPC, ~20ns, user-mode),
//     NO file/console I/O, NO event signalling. Queue full -> drop, never block.
//   * All formatting / dedup / colour / file / console / history work happens on a
//     BELOW_NORMAL priority worker thread that polls every 20ms.
//
// Lifecycle:
//   Call DreamLog::Instance().Init() once from your mod's init thread
//   (NOT from DllMain). Call Shutdown() from your unload path BEFORE FreeLibrary.
//   The singleton is intentionally leaked so logging is safe at any time,
//   including during static destruction / loader lock.
// ============================================================================

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <sal.h>
#include <cstdio>
#include <cstdarg>
#include <cstdint>
#include <cstring>
#include <cctype>
#include <string>
#include <vector>
#include <deque>
#include <mutex>
#include <atomic>
#include <new>
#include <algorithm>

#ifdef _MSC_VER
#define DREAM_PRINTF_FMT _Printf_format_string_
#else
#define DREAM_PRINTF_FMT
#endif

// Compile-time stripping (e.g. Release: -DDREAM_LOG_COMPILE_MIN_LEVEL=1 -DDREAM_LOG_COMPILE_MAX_VERBOSITY=2)
#ifndef DREAM_LOG_COMPILE_MIN_LEVEL
#define DREAM_LOG_COMPILE_MIN_LEVEL 0
#endif
#ifndef DREAM_LOG_COMPILE_MAX_VERBOSITY
#define DREAM_LOG_COMPILE_MAX_VERBOSITY 3
#endif

#include "DreamRawLog.hpp"

namespace dreamkurumi {

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
    Silent   = 0,
    Summary  = 1,
    Detailed = 2,
    Trace    = 3
};

struct LogEntry {
    std::string timestamp;
    DWORD threadId{0};
    LogLevel level{LogLevel::Info};
    LogChannel channel{LogChannel::Global};
    std::string message;      // message text only
    std::string rawFormatted; // full line incl. header + '\n'
};

using ExternalLogCallback = void (*)(const char* utf8Message);

class DreamLog {
public:
    // Never destroyed on purpose (see header comment).
    static DreamLog& Instance() {
        alignas(DreamLog) static unsigned char s_storage[sizeof(DreamLog)];
        static DreamLog* s_instance = new (s_storage) DreamLog();
        return *s_instance;
    }

    // ------------------------------------------------------------------------
    // Lifecycle
    // ------------------------------------------------------------------------
    void Init() {
        std::lock_guard<std::mutex> lk(m_sinkMutex);
        if (m_started.load(std::memory_order_acquire)) return;

        if (!m_pathSet) m_logPath = DefaultLogPath();
        if (m_fileEnabled && !m_logPath.empty()) {
            RotateFilesLocked(); // previous session -> .1, .2, .3
            OpenFileLocked();
        }

        if (!m_hConsole) {
            HANDLE hOut = CreateFileW(L"CONOUT$", GENERIC_READ | GENERIC_WRITE,
                                      FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                                      OPEN_EXISTING, 0, nullptr);
            if (hOut != INVALID_HANDLE_VALUE && hOut != nullptr) {
                m_hConsole = hOut;
                m_ownsConsoleHandles = true;
                EnableVT(m_hConsole);
            } else {
                hOut = GetStdHandle(STD_OUTPUT_HANDLE);
                if (hOut != INVALID_HANDLE_VALUE && hOut != nullptr) {
                    m_hConsole = hOut;
                    EnableVT(m_hConsole);
                }
            }
        }

        m_stop.store(false, std::memory_order_release);
        ResetEvent(m_stopEvent);
        m_thread = CreateThread(nullptr, 0, &DreamLog::ThreadProc, this, 0, nullptr);
        if (m_thread) SetThreadPriority(m_thread, THREAD_PRIORITY_BELOW_NORMAL);
        m_started.store(true, std::memory_order_release);

        char exe[MAX_PATH] = {0};
        GetModuleFileNameA(nullptr, exe, MAX_PATH);
        char buf[MAX_PATH + 96];
        int n = snprintf(buf, sizeof(buf), "===== dreamkurumi log session start | pid=%lu | %s =====",
                         (unsigned long)GetCurrentProcessId(), exe);
        if (n > 0) EnqueueInternal(LogChannel::Global, LogLevel::Info, buf, (size_t)n);
    }

    // processExiting=true: don't wait for the worker (safe under loader lock / ExitProcess)
    void Shutdown(bool processExiting = false) {
        if (!m_started.exchange(false, std::memory_order_acq_rel)) return;
        m_stop.store(true, std::memory_order_release);
        SetEvent(m_stopEvent);
        if (!processExiting && m_thread) {
            WaitForSingleObject(m_thread, 3000);
            CloseHandle(m_thread);
            m_thread = nullptr;
        }
        EmergencyFlush();
        std::lock_guard<std::mutex> lk(m_sinkMutex);
        CloseFileLocked();
    }

    // Drains the ring from the CALLING thread. Do not call from the game thread in normal operation.
    void Flush() {
        std::lock_guard<std::mutex> lk(m_sinkMutex);
        DrainLocked(kCapacity, true);
    }

    // Crash-safe: bounded wait, no throwing, writes straight to file via WriteFile.
    void EmergencyFlush() {
        for (int i = 0; i < 200; ++i) {
            if (m_sinkMutex.try_lock()) {
                DrainLocked(kCapacity, true);
                m_sinkMutex.unlock();
                return;
            }
            Sleep(1);
        }
    }

    // Optional: flush + stack trace on unhandled exception, then chain to previous filter.
    void InstallCrashFlush() {
        m_prevFilter = SetUnhandledExceptionFilter(&DreamLog::CrashFilter);
    }

    // ------------------------------------------------------------------------
    // Levels / channels
    // ------------------------------------------------------------------------
    void SetMinLogLevel(LogLevel level) { m_minLevel.store(level, std::memory_order_relaxed); }
    LogLevel GetMinLogLevel() const { return m_minLevel.load(std::memory_order_relaxed); }

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

    static bool TryParseChannel(const std::string& name, LogChannel& out) {
        std::string l = name;
        for (auto& c : l) c = (char)tolower((unsigned char)c);
        if (l == "global")                    { out = LogChannel::Global; return true; }
        if (l == "aoe" || l == "harvest")     { out = LogChannel::AoE;    return true; }
        if (l == "loot" || l == "pickup")     { out = LogChannel::Loot;   return true; }
        if (l == "admin" || l == "fishing")   { out = LogChannel::Admin;  return true; }
        if (l == "xp" || l == "xpshare")      { out = LogChannel::XP;     return true; }
        if (l == "hook")                      { out = LogChannel::Hook;   return true; }
        if (l == "net")                       { out = LogChannel::Net;    return true; }
        if (l == "ecs")                       { out = LogChannel::ECS;    return true; }
        return false;
    }

    static LogChannel ChannelFromString(const std::string& name) {
        LogChannel ch = LogChannel::Global;
        TryParseChannel(name, ch);
        return ch;
    }

    void SetChannelVerbosity(LogChannel ch, VerbosityLevel level, uint32_t durationSec = 0) {
        size_t idx = static_cast<size_t>(ch);
        if (idx >= static_cast<size_t>(LogChannel::Count)) return;
        uint8_t v = static_cast<uint8_t>(level);
        if (durationSec > 0) {
            m_channels[idx].level.store(v, std::memory_order_relaxed);
            m_channels[idx].expiryTimeMs.store(GetTickCount64() + durationSec * 1000ULL, std::memory_order_relaxed);
        } else {
            m_channels[idx].expiryTimeMs.store(0, std::memory_order_relaxed);
            m_channels[idx].baselineLevel.store(v, std::memory_order_relaxed);
            m_channels[idx].level.store(v, std::memory_order_relaxed);
        }
    }

    void SetChannelVerbosity(const std::string& name, int level, uint32_t durationSec = 0) {
        if (level < 0) level = 0;
        if (level > 3) level = 3;
        if (name == "all") {
            for (size_t i = 0; i < static_cast<size_t>(LogChannel::Count); ++i)
                SetChannelVerbosity(static_cast<LogChannel>(i), static_cast<VerbosityLevel>(level), durationSec);
            return;
        }
        SetChannelVerbosity(ChannelFromString(name), static_cast<VerbosityLevel>(level), durationSec);
    }

    VerbosityLevel GetChannelVerbosity(LogChannel ch) const {
        size_t idx = static_cast<size_t>(ch);
        if (idx >= static_cast<size_t>(LogChannel::Count)) return VerbosityLevel::Summary;
        return static_cast<VerbosityLevel>(m_channels[idx].level.load(std::memory_order_relaxed));
    }

    // The hot-path gate. Inline, 2 relaxed loads.
    inline bool IsEnabled(LogChannel ch, VerbosityLevel v, LogLevel lvl) const {
        if (static_cast<int>(lvl) < static_cast<int>(m_minLevel.load(std::memory_order_relaxed))) return false;
        size_t idx = static_cast<size_t>(ch);
        if (idx >= static_cast<size_t>(LogChannel::Count)) return true;
        return m_channels[idx].level.load(std::memory_order_relaxed) >= static_cast<uint8_t>(v);
    }

    inline bool IsChannelEnabled(LogChannel ch, VerbosityLevel v) const {
        size_t idx = static_cast<size_t>(ch);
        if (idx >= static_cast<size_t>(LogChannel::Count)) return true;
        return m_channels[idx].level.load(std::memory_order_relaxed) >= static_cast<uint8_t>(v);
    }

    // ------------------------------------------------------------------------
    // Sinks
    // ------------------------------------------------------------------------
    void SetExternalCallback(ExternalLogCallback cb) {
        std::lock_guard<std::mutex> lk(m_sinkMutex);
        m_externalCallback = cb;
    }

    // UTF-8 path. Empty string disables file logging. Opens in append mode (no rotation).
    void SetLogFile(const std::string& path) {
        std::lock_guard<std::mutex> lk(m_sinkMutex);
        CloseFileLocked();
        m_pathSet = true;
        m_fileEnabled = !path.empty();
        m_logPath = Utf8ToWide(path);
        if (m_fileEnabled && m_started.load(std::memory_order_acquire)) OpenFileLocked();
    }

    void SetColorsEnabled(bool enabled) { m_enableColors.store(enabled, std::memory_order_relaxed); }

    // OutputDebugString is slow with a debugger attached. Off by default.
    void SetDebugOutputEnabled(bool enabled) { m_debugOut.store(enabled, std::memory_order_relaxed); }

    bool AllocateConsoleWindow(const char* title = "dreamkurumiHT Diagnostics Console (Client)") {
        Init();
        std::lock_guard<std::mutex> lk(m_sinkMutex);
        if (m_consoleAllocated.load()) return true;

        bool ok = false;
        if (GetConsoleWindow() != NULL) {
            ok = true;
        } else if (AllocConsole()) {
            ok = true;
        } else if (GetLastError() == ERROR_ACCESS_DENIED) {
            ok = true;
        }
        if (!ok) return false;

        // Ensure stdio streams are tied to console buffer
        FILE* f = nullptr;
        freopen_s(&f, "CONOUT$", "w", stdout);
        freopen_s(&f, "CONOUT$", "w", stderr);
        freopen_s(&f, "CONIN$", "r", stdin);

        SetConsoleOutputCP(CP_UTF8);
        SetConsoleCP(CP_UTF8);

        // Clean up previously allocated handles if reallocating
        if (m_ownsConsoleHandles) {
            if (m_hConsole && m_hConsole != INVALID_HANDLE_VALUE && m_hConsole != GetStdHandle(STD_OUTPUT_HANDLE)) {
                CloseHandle(m_hConsole);
            }
            if (m_hConsoleIn && m_hConsoleIn != INVALID_HANDLE_VALUE && m_hConsoleIn != GetStdHandle(STD_INPUT_HANDLE)) {
                CloseHandle(m_hConsoleIn);
            }
            m_ownsConsoleHandles = false;
        }

        // Open CONOUT$ directly to guarantee a valid output handle in Windows GUI processes
        bool ownsOut = false;
        HANDLE hOut = CreateFileW(L"CONOUT$", GENERIC_READ | GENERIC_WRITE,
                                  FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                                  OPEN_EXISTING, 0, nullptr);
        if (hOut == INVALID_HANDLE_VALUE || hOut == nullptr) {
            hOut = GetStdHandle(STD_OUTPUT_HANDLE);
        } else {
            SetStdHandle(STD_OUTPUT_HANDLE, hOut);
            ownsOut = true;
        }

        if (!EnableVT(hOut)) {
            // Wine / Proton Fallback: If VTP fails to activate, cleanly fallback to monochrome output
            m_enableColors.store(false, std::memory_order_relaxed);
        }

        // Open CONIN$ and configure QuickEdit Mode for mouse selection & copy
        bool ownsIn = false;
        HANDLE hIn = CreateFileW(L"CONIN$", GENERIC_READ | GENERIC_WRITE,
                                 FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                                 OPEN_EXISTING, 0, nullptr);
        if (hIn == INVALID_HANDLE_VALUE || hIn == nullptr) {
            hIn = GetStdHandle(STD_INPUT_HANDLE);
        } else {
            SetStdHandle(STD_INPUT_HANDLE, hIn);
            ownsIn = true;
        }

        DWORD inMode = 0;
        if (hIn != INVALID_HANDLE_VALUE && hIn != nullptr && GetConsoleMode(hIn, &inMode)) {
            // ENABLE_QUICK_EDIT_MODE + ENABLE_EXTENDED_FLAGS allows selecting text with mouse and copying (Ctrl+C / Right-Click / Enter)
            SetConsoleMode(hIn, inMode | ENABLE_QUICK_EDIT_MODE | ENABLE_EXTENDED_FLAGS | ENABLE_INSERT_MODE);
        }

        if (title) SetConsoleTitleA(title);

        m_hConsole = hOut;
        m_hConsoleIn = hIn;
        m_ownsConsoleHandles = (ownsOut || ownsIn);
        m_consoleAllocated.store(true);
        m_enableColors.store(true, std::memory_order_relaxed);

        static const char banner[] =
            "\033[95m======================================================================\033[0m\n"
            "\033[96m  dreamkurumiHT Diagnostics Console (Client Mode)\033[0m\n"
            "\033[90m  Monitoring Enshrouded Hooks, LiveGameSettings & Crash Guard...\033[0m\n"
            "\033[93m  Tip: QuickEdit Enabled (Select & Press Enter to Copy) | F9 Menu\033[0m\n"
            "\033[95m======================================================================\033[0m\n\n";
        DWORD w = 0;
        WriteConsoleA(m_hConsole, banner, (DWORD)(sizeof(banner) - 1), &w, nullptr);
        return true;
    }

    void FreeConsoleWindow() {
        std::lock_guard<std::mutex> lk(m_sinkMutex);
        if (m_consoleAllocated.load()) {
            if (m_ownsConsoleHandles) {
                if (m_hConsole && m_hConsole != INVALID_HANDLE_VALUE && m_hConsole != GetStdHandle(STD_OUTPUT_HANDLE)) {
                    CloseHandle(m_hConsole);
                }
                if (m_hConsoleIn && m_hConsoleIn != INVALID_HANDLE_VALUE && m_hConsoleIn != GetStdHandle(STD_INPUT_HANDLE)) {
                    CloseHandle(m_hConsoleIn);
                }
                m_ownsConsoleHandles = false;
            }
            FreeConsole();
            m_hConsole = nullptr;
            m_hConsoleIn = nullptr;
            m_consoleAllocated.store(false);
        }
    }

    bool IsConsoleAllocated() const { return m_consoleAllocated.load(); }

    // ------------------------------------------------------------------------
    // History / grep (filled by the worker, never by the game thread)
    // ------------------------------------------------------------------------
    void SetMaxHistory(size_t count) {
        std::lock_guard<std::mutex> lk(m_historyMutex);
        m_maxHistory = count;
        while (m_history.size() > m_maxHistory) m_history.pop_front();
    }

    std::vector<LogEntry> GetRecentLogs(size_t maxCount = 100) {
        std::lock_guard<std::mutex> lk(m_historyMutex);
        size_t count = (maxCount < m_history.size()) ? maxCount : m_history.size();
        return std::vector<LogEntry>(m_history.end() - count, m_history.end());
    }

    std::vector<LogEntry> GrepLogs(const std::string& pattern, size_t maxCount = 50) {
        std::string lp = pattern;
        for (auto& c : lp) c = (char)tolower((unsigned char)c);
        std::vector<LogEntry> matches;
        std::lock_guard<std::mutex> lk(m_historyMutex);
        for (auto it = m_history.rbegin(); it != m_history.rend(); ++it) {
            std::string lm = it->rawFormatted;
            for (auto& c : lm) c = (char)tolower((unsigned char)c);
            if (lm.find(lp) != std::string::npos) {
                matches.push_back(*it);
                if (matches.size() >= maxCount) break;
            }
        }
        std::reverse(matches.begin(), matches.end());
        return matches;
    }

    void ClearHistory() {
        std::lock_guard<std::mutex> lk(m_historyMutex);
        m_history.clear();
    }

    // ------------------------------------------------------------------------
    // Logging entry points
    // ------------------------------------------------------------------------
    void Log(LogLevel level, DREAM_PRINTF_FMT const char* format, ...) {
        if (!IsEnabled(LogChannel::Global, VerbosityLevel::Summary, level)) return;
        va_list args;
        va_start(args, format);
        LogChannelV(LogChannel::Global, VerbosityLevel::Summary, level, format, args);
        va_end(args);
    }

    void LogChannelMsg(LogChannel ch, VerbosityLevel verbosity, LogLevel level,
                       DREAM_PRINTF_FMT const char* format, ...) {
        if (!IsEnabled(ch, verbosity, level)) return;
        va_list args;
        va_start(args, format);
        LogChannelV(ch, verbosity, level, format, args);
        va_end(args);
    }

    void LogV(LogLevel level, const char* format, va_list args) {
        LogChannelV(LogChannel::Global, VerbosityLevel::Summary, level, format, args);
    }

    void LogChannelV(LogChannel ch, VerbosityLevel verbosity, LogLevel level, const char* format, va_list args) {
        if (!IsEnabled(ch, verbosity, level)) return;
        char buf[kMsgMax];
        int n = vsnprintf(buf, sizeof(buf), format, args);
        if (n < 0) {
            memcpy(buf, "<format error>", 14);
            n = 14;
        } else if (static_cast<size_t>(n) >= sizeof(buf)) {
            n = static_cast<int>(sizeof(buf)) - 1;
            buf[n - 3] = buf[n - 2] = buf[n - 1] = '.';
        }
        Enqueue(ch, level, buf, static_cast<size_t>(n));
    }

    // Readable-checked hex dump, one log record per 16 bytes (max 4 KiB).
    static void HexDump(const char* title, const void* data, size_t length,
                        LogLevel level = LogLevel::Info, LogChannel ch = LogChannel::Global) {
        DreamLog& L = Instance();
        if (!data || length == 0 || !L.IsEnabled(ch, VerbosityLevel::Summary, level)) return;
        if (length > 4096) length = 4096;
        if (!IsReadable(data, length)) {
            L.LogChannelMsg(ch, VerbosityLevel::Summary, LogLevel::Warn,
                            "HexDump '%s': %p (+%zu) is not readable memory", title ? title : "Memory", data, length);
            return;
        }
        L.LogChannelMsg(ch, VerbosityLevel::Summary, level, "=== Hex Dump: %s (%zu bytes at %p) ===",
                        title ? title : "Memory", length, data);
        const auto* b = static_cast<const uint8_t*>(data);
        static const char* hx = "0123456789ABCDEF";
        for (size_t i = 0; i < length; i += 16) {
            char line[128];
            size_t p = 0;
            p += (size_t)snprintf(line, sizeof(line), "  %08zX: ", i);
            for (size_t j = 0; j < 16; ++j) {
                if (i + j < length) {
                    uint8_t v = b[i + j];
                    line[p++] = hx[v >> 4];
                    line[p++] = hx[v & 15];
                    line[p++] = ' ';
                } else {
                    line[p++] = ' '; line[p++] = ' '; line[p++] = ' ';
                }
                if (j == 7) line[p++] = ' ';
            }
            line[p++] = '|'; line[p++] = ' ';
            for (size_t j = 0; j < 16 && i + j < length; ++j) {
                uint8_t c = b[i + j];
                line[p++] = (c >= 32 && c <= 126) ? (char)c : '.';
            }
            line[p] = 0;
            L.LogChannelMsg(ch, VerbosityLevel::Summary, level, "%s", line);
        }
    }

    uint64_t QpcFrequency() const { return m_qpcFreq; }
    uint32_t DroppedTotal() const { return m_droppedTotal.load(std::memory_order_relaxed); }

    static const char* LevelName(LogLevel l) {
        switch (l) {
            case LogLevel::Trace: return "TRACE";
            case LogLevel::Debug: return "DEBUG";
            case LogLevel::Info:  return "INFO ";
            case LogLevel::Warn:  return "WARN ";
            case LogLevel::Error: return "ERROR";
            case LogLevel::Fatal: return "FATAL";
            default:              return "INFO ";
        }
    }

    static const char* LevelBadgeColor(LogLevel l) {
        switch (l) {
            case LogLevel::Trace: return "\033[90m";         // Dark gray badge
            case LogLevel::Debug: return "\033[90m";         // Dark gray badge
            case LogLevel::Info:  return "\033[32m";         // Emerald Green badge
            case LogLevel::Warn:  return "\033[1;33m";       // Bold Gold / Amber badge
            case LogLevel::Error: return "\033[1;31m";       // Bold Red badge
            case LogLevel::Fatal: return "\033[1;97;41m";    // Bold White text on Red background
            default:              return "\033[0m";
        }
    }

    static const char* MessageColor(LogLevel l) {
        switch (l) {
            case LogLevel::Trace: return "\033[90m";   // Muted gray
            case LogLevel::Debug: return "\033[90m";   // Muted gray
            case LogLevel::Info:  return "\033[0m";    // Default terminal white/light
            case LogLevel::Warn:  return "\033[93m";   // Light yellow for warning text
            case LogLevel::Error: return "\033[91m";   // Light red for error text
            case LogLevel::Fatal: return "\033[1;91m"; // Bold bright red for fatal text
            default:              return "\033[0m";
        }
    }

    static const char* LevelColor(LogLevel l) {
        return LevelBadgeColor(l);
    }

    static void PrintFormattedEntry(const LogEntry& entry) {
        if (Instance().m_enableColors.load(std::memory_order_relaxed)) {
            printf("\033[90m[%s] [T%05lu]\033[0m \033[36m[%-6s]\033[0m %s[%s]\033[0m %s%s\033[0m\n",
                   entry.timestamp.c_str(),
                   (unsigned long)entry.threadId,
                   ChannelToString(entry.channel),
                   LevelBadgeColor(entry.level),
                   LevelName(entry.level),
                   MessageColor(entry.level),
                   entry.message.c_str());
        } else {
            printf("%s", entry.rawFormatted.c_str());
        }
    }

private:
    // ------------------------------------------------------------------------
    // Ring buffer (Vyukov bounded MPSC style; producers = any thread, consumer = worker)
    // ------------------------------------------------------------------------
    static constexpr size_t   kCapacity     = 4096;           // power of two
    static constexpr uint32_t kMask         = (uint32_t)kCapacity - 1;
    static constexpr size_t   kMsgMax       = 2048;
    static constexpr size_t   kMaxFileBytes = 10u * 1024u * 1024u;
    static constexpr int      kKeepFiles    = 3;

    struct alignas(64) Slot {
        std::atomic<uint32_t> seq;
        DWORD    tid;
        uint64_t qpc;
        uint16_t len;
        uint8_t  level;
        uint8_t  channel;
        char     msg[kMsgMax];
    };

    struct ChannelConfig {
        std::atomic<uint8_t>  level{1};
        std::atomic<uint8_t>  baselineLevel{1};
        std::atomic<uint64_t> expiryTimeMs{0};
    };

    DreamLog() {
        LARGE_INTEGER f, q;
        QueryPerformanceFrequency(&f);
        QueryPerformanceCounter(&q);
        m_qpcFreq = (uint64_t)f.QuadPart;
        m_qpcBase = (uint64_t)q.QuadPart;
        SYSTEMTIME st;
        GetLocalTime(&st);
        m_baseMsOfDay = (((uint64_t)st.wHour * 60 + st.wMinute) * 60 + st.wSecond) * 1000 + st.wMilliseconds;

        for (uint32_t i = 0; i < kCapacity; ++i) m_ring[i].seq.store(i, std::memory_order_relaxed);
        m_stopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        EnableVT(GetStdHandle(STD_OUTPUT_HANDLE));
    }
    DreamLog(const DreamLog&) = delete;
    DreamLog& operator=(const DreamLog&) = delete;

    // ---- producer side -------------------------------------------------------
    inline void Enqueue(LogChannel ch, LogLevel level, const char* msg, size_t len) {
        if (!msg || len == 0) return;
        char cleanBuf[kMsgMax];
        LogLevel detectedLevel = level;
        bool foundLevel = false;
        size_t strippedLen = RawLogStripLegacy(msg, len, cleanBuf, sizeof(cleanBuf), &detectedLevel, &foundLevel);
        if (strippedLen > 0) {
            msg = cleanBuf;
            len = strippedLen;
            if (foundLevel) level = detectedLevel;
        }
        if (len > kMsgMax) len = kMsgMax;
        uint32_t pos = m_head.load(std::memory_order_relaxed);
        for (;;) {
            Slot& s = m_ring[pos & kMask];
            uint32_t seq = s.seq.load(std::memory_order_acquire);
            int32_t diff = (int32_t)(seq - pos);
            if (diff == 0) {
                if (m_head.compare_exchange_weak(pos, pos + 1, std::memory_order_relaxed)) {
                    LARGE_INTEGER q;
                    QueryPerformanceCounter(&q);
                    s.qpc = (uint64_t)q.QuadPart;
                    s.tid = GetCurrentThreadId();
                    s.level = (uint8_t)level;
                    s.channel = (uint8_t)ch;
                    s.len = (uint16_t)len;
                    memcpy(s.msg, msg, len);
                    s.seq.store(pos + 1, std::memory_order_release);
                    return;
                }
            } else if (diff < 0) {
                m_dropped.fetch_add(1, std::memory_order_relaxed); // full: drop, never block the game
                return;
            } else {
                pos = m_head.load(std::memory_order_relaxed);
            }
        }
    }

    void EnqueueInternal(LogChannel ch, LogLevel lvl, const char* msg, size_t len) { Enqueue(ch, lvl, msg, len); }

    // ---- consumer side (always under m_sinkMutex) ----------------------------
    size_t DrainLocked(size_t maxItems, bool finalFlush) {
        uint32_t d = m_dropped.exchange(0, std::memory_order_relaxed);
        if (d) {
            m_droppedTotal.fetch_add(d, std::memory_order_relaxed);
            char b[96];
            int n = snprintf(b, sizeof(b), "[DreamLog] queue full: dropped %u message(s)", d);
            LARGE_INTEGER q; QueryPerformanceCounter(&q);
            EmitLocked((uint64_t)q.QuadPart, GetCurrentThreadId(), LogLevel::Warn, LogChannel::Global, b, (size_t)n);
        }

        size_t n = 0;
        uint32_t pos = m_tail.load(std::memory_order_relaxed);
        while (n < maxItems) {
            Slot& s = m_ring[pos & kMask];
            if (s.seq.load(std::memory_order_acquire) != pos + 1) break;
            ProcessSlotLocked(s);
            s.seq.store(pos + (uint32_t)kCapacity, std::memory_order_release);
            ++pos;
            ++n;
        }
        m_tail.store(pos, std::memory_order_relaxed);

        if (finalFlush) FlushRepeatLocked();
        FlushOutputsLocked();
        return n;
    }

    void ProcessSlotLocked(const Slot& s) {
        LogChannel ch = (LogChannel)s.channel;
        LogLevel lvl = (LogLevel)s.level;
        uint64_t ms = QpcToMs(s.qpc);

        // Anti-spam dedup runs on the worker, so it costs the game thread nothing.
        if (m_hasLast && ch == m_lastCh && lvl == m_lastLevel && s.len == m_lastMsg.size() &&
            memcmp(s.msg, m_lastMsg.data(), s.len) == 0 && (int64_t)(ms - m_lastEmitMs) < 1000) {
            ++m_repeatCount;
            m_lastRepeatQpc = s.qpc;
            m_lastRepeatTid = s.tid;
            return;
        }
        FlushRepeatLocked();
        EmitLocked(s.qpc, s.tid, lvl, ch, s.msg, s.len);
        m_lastMsg.assign(s.msg, s.len);
        m_lastCh = ch;
        m_lastLevel = lvl;
        m_lastEmitMs = ms;
        m_hasLast = true;
    }

    void FlushRepeatLocked() {
        if (m_repeatCount > 0) {
            char b[96];
            int n = snprintf(b, sizeof(b), "(previous message repeated %zu more time(s))", m_repeatCount);
            EmitLocked(m_lastRepeatQpc, m_lastRepeatTid, m_lastLevel, m_lastCh, b, (size_t)n);
            m_repeatCount = 0;
        }
        m_hasLast = false;
    }

    void FlushStaleRepeatLocked() {
        if (m_repeatCount == 0) return;
        LARGE_INTEGER q; QueryPerformanceCounter(&q);
        if ((int64_t)(QpcToMs((uint64_t)q.QuadPart) - m_lastEmitMs) >= 1000) FlushRepeatLocked();
    }

    void EmitLocked(uint64_t qpc, DWORD tid, LogLevel level, LogChannel ch, const char* msg, size_t len) {
        RawLogReentryGuard reentryGuard;
        if (!reentryGuard.Acquired()) return;

        uint64_t dayMs = (m_baseMsOfDay + QpcToMs(qpc)) % 86400000ULL;
        char ts[16];
        snprintf(ts, sizeof(ts), "%02u:%02u:%02u.%03u",
                 (unsigned)(dayMs / 3600000ULL), (unsigned)(dayMs / 60000ULL % 60),
                 (unsigned)(dayMs / 1000ULL % 60), (unsigned)(dayMs % 1000ULL));

        // 1. Plain text format for disk file logging (zero ANSI codes, clean & aligned)
        char fileHdr[96];
        int fhl = snprintf(fileHdr, sizeof(fileHdr), "[%s] [T%05lu] [%-6s] [%s] ",
                           ts, (unsigned long)tid, ChannelToString(ch), LevelName(level));
        m_line.assign(fileHdr, (size_t)fhl);
        m_line.append(msg, len);
        m_line.push_back('\n');

        m_fileBuf += m_line;

        // 2. Modern segmented color format for console (Visual Hierarchy)
        if (m_hConsole) {
            if (m_enableColors.load(std::memory_order_relaxed)) {
                char conHdr[144];
                int chl = snprintf(conHdr, sizeof(conHdr),
                                   "\033[90m[%s] [T%05lu]\033[0m \033[36m[%-6s]\033[0m %s[%s]\033[0m %s",
                                   ts, (unsigned long)tid,
                                   ChannelToString(ch),
                                   LevelBadgeColor(level), LevelName(level),
                                   MessageColor(level));
                m_conBuf.append(conHdr, (size_t)chl);
                m_conBuf.append(msg, len);
                m_conBuf.append("\033[0m\n");
            } else {
                m_conBuf += m_line;
            }
        }
        if (m_debugOut.load(std::memory_order_relaxed)) OutputDebugStringA(m_line.c_str());
        // Forward to host callback if no local console handle is active (avoids duplicate uncolored prints & double timestamps)
        if (m_externalCallback && !m_hConsole) m_externalCallback(m_line.c_str());

        std::lock_guard<std::mutex> hl2(m_historyMutex);
        if (m_maxHistory > 0) {
            if (m_history.size() >= m_maxHistory) m_history.pop_front();
            LogEntry e;
            e.timestamp = ts;
            e.threadId = tid;
            e.level = level;
            e.channel = ch;
            e.message.assign(msg, len);
            e.rawFormatted = m_line;
            m_history.push_back(std::move(e));
        }
    }

    void FlushOutputsLocked() {
        // One WriteFile + (chunked) WriteConsole per cycle instead of printf+fflush per line.
        if (!m_fileBuf.empty()) {
            if (m_hFile != INVALID_HANDLE_VALUE) {
                DWORD w = 0;
                WriteFile(m_hFile, m_fileBuf.data(), (DWORD)m_fileBuf.size(), &w, nullptr);
                m_fileBytes += m_fileBuf.size();
            }
            m_fileBuf.clear();
            if (m_fileBytes > kMaxFileBytes) { RotateFilesLocked(); OpenFileLocked(); }
        }
        if (!m_conBuf.empty()) {
            if (m_hConsole) {
                const char* p = m_conBuf.data();
                size_t left = m_conBuf.size();
                while (left > 0) {
                    size_t chunk = left > 16384 ? 16384 : left;
                    if (chunk < left) {
                        size_t k = chunk;
                        while (k > 0 && p[k - 1] != '\n') --k;
                        if (k > 0) chunk = k;
                    }
                    DWORD w = 0;
                    if (!WriteConsoleA(m_hConsole, p, (DWORD)chunk, &w, nullptr)) {
                        if (!WriteFile(m_hConsole, p, (DWORD)chunk, &w, nullptr)) break;
                    }
                    p += chunk;
                    left -= chunk;
                }
            }
            m_conBuf.clear();
        }
        if (m_fileBuf.capacity() > (1u << 20)) std::string().swap(m_fileBuf);
        if (m_conBuf.capacity() > (1u << 20)) std::string().swap(m_conBuf);
    }

    // ---- worker ---------------------------------------------------------------
    static DWORD WINAPI ThreadProc(LPVOID p) {
        static_cast<DreamLog*>(p)->WorkerLoop();
        return 0;
    }

    void WorkerLoop() {
        while (!m_stop.load(std::memory_order_acquire)) {
            size_t n;
            {
                std::lock_guard<std::mutex> lk(m_sinkMutex);
                n = DrainLocked(2048, false);
                FlushStaleRepeatLocked();
                FlushOutputsLocked();
            }
            CheckTimedVerbosityExpirations();
            if (n >= 2048) continue; // backlog: keep draining without sleeping
            WaitForSingleObject(m_stopEvent, 20);
        }
    }

    void CheckTimedVerbosityExpirations() {
        uint64_t now = GetTickCount64();
        for (size_t i = 0; i < static_cast<size_t>(LogChannel::Count); ++i) {
            uint64_t exp = m_channels[i].expiryTimeMs.load(std::memory_order_relaxed);
            if (exp > 0 && now >= exp) {
                uint8_t base = m_channels[i].baselineLevel.load(std::memory_order_relaxed);
                m_channels[i].level.store(base, std::memory_order_relaxed);
                m_channels[i].expiryTimeMs.store(0, std::memory_order_relaxed);
                char b[128];
                int n = snprintf(b, sizeof(b), "[DreamLog] Timed verbosity burst expired for channel '%s'. Reverted to level %u.",
                                 ChannelToString(static_cast<LogChannel>(i)), (unsigned)base);
                EnqueueInternal(LogChannel::Global, LogLevel::Info, b, (size_t)n);
            }
        }
    }

    // ---- file handling ----------------------------------------------------------
    void OpenFileLocked() {
        if (m_hFile != INVALID_HANDLE_VALUE) return;
        m_hFile = CreateFileW(m_logPath.c_str(), FILE_APPEND_DATA,
                              FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                              nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        m_fileBytes = 0;
        if (m_hFile != INVALID_HANDLE_VALUE) {
            LARGE_INTEGER sz;
            if (GetFileSizeEx(m_hFile, &sz)) m_fileBytes = (size_t)sz.QuadPart;
        }
    }

    void CloseFileLocked() {
        if (m_hFile != INVALID_HANDLE_VALUE) {
            CloseHandle(m_hFile);
            m_hFile = INVALID_HANDLE_VALUE;
        }
    }

    // dreamkurumi.log -> .1 -> .2 -> .3 (oldest deleted)
    void RotateFilesLocked() {
        CloseFileLocked();
        if (m_logPath.empty()) return;
        DeleteFileW((m_logPath + L"." + std::to_wstring(kKeepFiles)).c_str());
        for (int i = kKeepFiles - 1; i >= 1; --i)
            MoveFileExW((m_logPath + L"." + std::to_wstring(i)).c_str(),
                        (m_logPath + L"." + std::to_wstring(i + 1)).c_str(), MOVEFILE_REPLACE_EXISTING);
        MoveFileExW(m_logPath.c_str(), (m_logPath + L".1").c_str(), MOVEFILE_REPLACE_EXISTING);
    }

    static void ModuleAnchor() {}

    static std::wstring DefaultLogPath() {
        HMODULE h = nullptr;
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCWSTR>(&DreamLog::ModuleAnchor), &h);
        wchar_t buf[MAX_PATH * 2];
        DWORD n = GetModuleFileNameW(h, buf, (DWORD)(sizeof(buf) / sizeof(buf[0])));
        if (n == 0 || n >= sizeof(buf) / sizeof(buf[0])) return L"dreamkurumi.log";
        std::wstring p(buf, n);
        size_t slash = p.find_last_of(L"\\/");
        return (slash == std::wstring::npos ? std::wstring() : p.substr(0, slash + 1)) + L"dreamkurumi.log";
    }

    // ---- helpers ------------------------------------------------------------------
    static std::wstring Utf8ToWide(const std::string& s) {
        if (s.empty()) return std::wstring();
        int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
        std::wstring w((size_t)n, L'\0');
        MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &w[0], n);
        return w;
    }

    static bool EnableVT(HANDLE h) {
        DWORD mode = 0;
        if (h != INVALID_HANDLE_VALUE && h != nullptr && GetConsoleMode(h, &mode))
            return SetConsoleMode(h, mode | 0x0004 /* ENABLE_VIRTUAL_TERMINAL_PROCESSING */ | 0x0001 /* ENABLE_PROCESSED_OUTPUT */) != FALSE;
        return false;
    }

    uint64_t QpcToMs(uint64_t q) const {
        uint64_t dq = q > m_qpcBase ? q - m_qpcBase : 0;
        return (dq / m_qpcFreq) * 1000ULL + (dq % m_qpcFreq) * 1000ULL / m_qpcFreq;
    }

    static bool IsReadable(const void* p, size_t len) {
        const uint8_t* a = static_cast<const uint8_t*>(p);
        const uint8_t* end = a + len;
        while (a < end) {
            MEMORY_BASIC_INFORMATION mbi;
            if (!VirtualQuery(a, &mbi, sizeof(mbi))) return false;
            if (mbi.State != MEM_COMMIT || mbi.Protect == 0 ||
                (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD))) return false;
            a = static_cast<const uint8_t*>(mbi.BaseAddress) + mbi.RegionSize;
        }
        return true;
    }

    static LONG WINAPI CrashFilter(EXCEPTION_POINTERS* ep) {
        DreamLog& L = Instance();
        if (ep && ep->ExceptionRecord) {
            char b[160];
            int n = snprintf(b, sizeof(b), "UNHANDLED EXCEPTION 0x%08lX at %p",
                             (unsigned long)ep->ExceptionRecord->ExceptionCode, ep->ExceptionRecord->ExceptionAddress);
            L.EnqueueInternal(LogChannel::Global, LogLevel::Fatal, b, (size_t)n);

            void* frames[24];
            USHORT cnt = CaptureStackBackTrace(0, 24, frames, nullptr);
            for (USHORT i = 0; i < cnt; ++i) {
                HMODULE mod = nullptr;
                char name[MAX_PATH] = "?";
                if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                       (LPCSTR)frames[i], &mod) && mod) {
                    char full[MAX_PATH];
                    if (GetModuleFileNameA(mod, full, MAX_PATH)) {
                        const char* s = strrchr(full, '\\');
                        strncpy_s(name, s ? s + 1 : full, _TRUNCATE);
                    }
                }
                n = snprintf(b, sizeof(b), "  #%02u %s+0x%llX (%p)", (unsigned)i, name,
                             (unsigned long long)((uintptr_t)frames[i] - (uintptr_t)mod), frames[i]);
                L.EnqueueInternal(LogChannel::Global, LogLevel::Fatal, b, (size_t)n);
            }
        }
        L.EmergencyFlush();
        if (L.m_prevFilter) return L.m_prevFilter(ep);
        return EXCEPTION_CONTINUE_SEARCH;
    }

    // ---- state ---------------------------------------------------------------------
    // Producer-hot state, each on its own cache line.
    alignas(64) std::atomic<uint32_t> m_head{0};
    alignas(64) std::atomic<uint32_t> m_tail{0};
    alignas(64) std::atomic<uint32_t> m_dropped{0};
    std::atomic<uint32_t> m_droppedTotal{0};

    std::atomic<LogLevel> m_minLevel{LogLevel::Debug};
    ChannelConfig m_channels[static_cast<size_t>(LogChannel::Count)];
    Slot m_ring[kCapacity];

    // Time base
    uint64_t m_qpcFreq{1};
    uint64_t m_qpcBase{0};
    uint64_t m_baseMsOfDay{0};

    // Worker
    HANDLE m_thread{nullptr};
    HANDLE m_stopEvent{nullptr};
    std::atomic<bool> m_stop{false};
    std::atomic<bool> m_started{false};

    // Sinks (guarded by m_sinkMutex; never touched by game threads)
    std::mutex m_sinkMutex;
    ExternalLogCallback m_externalCallback{nullptr};
    std::wstring m_logPath;
    bool m_pathSet{false};
    bool m_fileEnabled{true};
    HANDLE m_hFile{INVALID_HANDLE_VALUE};
    size_t m_fileBytes{0};
    HANDLE m_hConsole{nullptr};
    HANDLE m_hConsoleIn{nullptr};
    bool m_ownsConsoleHandles{false};
    std::atomic<bool> m_consoleAllocated{false};
    std::atomic<bool> m_enableColors{true};
    std::atomic<bool> m_debugOut{false};
    std::string m_fileBuf, m_conBuf, m_line;

    // Dedup (worker only)
    std::string m_lastMsg;
    LogChannel m_lastCh{LogChannel::Global};
    LogLevel m_lastLevel{LogLevel::Info};
    uint64_t m_lastEmitMs{0};
    uint64_t m_lastRepeatQpc{0};
    DWORD m_lastRepeatTid{0};
    size_t m_repeatCount{0};
    bool m_hasLast{false};

    // History
    std::mutex m_historyMutex;
    std::deque<LogEntry> m_history;
    size_t m_maxHistory{2000};

    LPTOP_LEVEL_EXCEPTION_FILTER m_prevFilter{nullptr};
};

// ----------------------------------------------------------------------------
// RAII timer: logs only if the scope took >= thresholdUs. `name` must be a literal.
// ----------------------------------------------------------------------------
class ScopedTimer {
public:
    explicit ScopedTimer(const char* name, uint32_t thresholdUs = 1000,
                         LogChannel ch = LogChannel::Global)
        : m_name(name), m_thresholdUs(thresholdUs), m_ch(ch) {
        LARGE_INTEGER q; QueryPerformanceCounter(&q);
        m_start = (uint64_t)q.QuadPart;
    }
    ~ScopedTimer() {
        LARGE_INTEGER q; QueryPerformanceCounter(&q);
        DreamLog& L = DreamLog::Instance();
        uint64_t us = ((uint64_t)q.QuadPart - m_start) * 1000000ULL / L.QpcFrequency();
        if (us >= m_thresholdUs)
            L.LogChannelMsg(m_ch, VerbosityLevel::Summary, LogLevel::Warn, "[perf] %s took %.3f ms", m_name, us / 1000.0);
    }
    ScopedTimer(const ScopedTimer&) = delete;
    ScopedTimer& operator=(const ScopedTimer&) = delete;
private:
    const char* m_name;
    uint32_t m_thresholdUs;
    LogChannel m_ch;
    uint64_t m_start;
};

} // namespace dreamkurumi

// ============================================================================
// Macros: the level/channel check happens BEFORE the arguments are evaluated.
// ============================================================================
#define DREAM_LOG_CHANNEL(ch, verbosity, level, fmt, ...) \
    do { \
        if ((int)(level) >= DREAM_LOG_COMPILE_MIN_LEVEL && (int)(verbosity) <= DREAM_LOG_COMPILE_MAX_VERBOSITY) { \
            auto& _dl = ::dreamkurumi::DreamLog::Instance(); \
            if (_dl.IsEnabled(ch, verbosity, level)) \
                _dl.LogChannelMsg(ch, verbosity, level, fmt, ##__VA_ARGS__); \
        } \
    } while (0)

#define DREAM_LOG_AT(level, fmt, ...) \
    DREAM_LOG_CHANNEL(::dreamkurumi::LogChannel::Global, ::dreamkurumi::VerbosityLevel::Summary, level, fmt, ##__VA_ARGS__)

#define DREAM_LOG_DEBUG(fmt, ...) DREAM_LOG_AT(::dreamkurumi::LogLevel::Debug, fmt, ##__VA_ARGS__)
#define DREAM_LOG_INFO(fmt, ...)  DREAM_LOG_AT(::dreamkurumi::LogLevel::Info,  fmt, ##__VA_ARGS__)
#define DREAM_LOG_WARN(fmt, ...)  DREAM_LOG_AT(::dreamkurumi::LogLevel::Warn,  fmt, ##__VA_ARGS__)
#define DREAM_LOG_ERROR(fmt, ...) DREAM_LOG_AT(::dreamkurumi::LogLevel::Error, fmt, ##__VA_ARGS__)
#define DREAM_LOG_FATAL(fmt, ...) DREAM_LOG_AT(::dreamkurumi::LogLevel::Fatal, fmt, ##__VA_ARGS__)

// For per-frame hooks: emit only every Nth call / at most once per `ms` milliseconds (per call site).
#define DREAM_LOG_EVERY_N(n, ch, verbosity, level, fmt, ...) \
    do { \
        static std::atomic<uint32_t> _dc{0}; \
        if (_dc.fetch_add(1, std::memory_order_relaxed) % (uint32_t)(n) == 0) \
            DREAM_LOG_CHANNEL(ch, verbosity, level, fmt, ##__VA_ARGS__); \
    } while (0)

#define DREAM_LOG_THROTTLE_MS(ms, ch, verbosity, level, fmt, ...) \
    do { \
        static std::atomic<uint64_t> _dt{0}; \
        uint64_t _now = GetTickCount64(); \
        uint64_t _prev = _dt.load(std::memory_order_relaxed); \
        if (_now - _prev >= (uint64_t)(ms) && _dt.compare_exchange_strong(_prev, _now, std::memory_order_relaxed)) \
            DREAM_LOG_CHANNEL(ch, verbosity, level, fmt, ##__VA_ARGS__); \
    } while (0)

#define DREAM_PERF_SCOPE(name, thresholdUs) ::dreamkurumi::ScopedTimer _dream_scope_timer(name, thresholdUs)
