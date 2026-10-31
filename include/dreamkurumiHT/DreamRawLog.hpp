#pragma once
// DreamRawLog.hpp - raw log message ABI between dinput8 (host) and dreamkurumiHT (core)
//
// Rules this header enforces:
//   * The core is the ONLY place that formats text (timestamp, level colour, thread tag, channel).
//     Everybody else sends a DreamRawLogMessage: level + source + plain UTF-8 text, no decoration.
//   * "No timestamp" means no FORMATTED timestamp. Each message carries a raw monotonic tick taken at the
//     moment of emission, so messages buffered during early boot still show their real time when the core
//     finally prints them (RawLogClock converts ticks -> HH:MM:SS.mmm).
//   * Emitting never allocates and never takes a lock: safe to call from DllMain (CRT snprintf/QPC only).
//
// Ownership:
//   dinput8 owns a DreamBootLog. Before the core exists, Emit() fills a fixed 256-slot buffer.
//   The core obtains DreamLogBridgeV1 (pass the pointer through your existing plugin/host context) and calls
//   attachSink(): buffered messages are delivered in order, then emits go straight to the sink.
//   The sink runs on the EMITTING thread: it must only copy into the core's ring buffer (no I/O, no locks
//   that a logging thread could already hold).
//
// Re-entry guard: RawLogReentryGuard is thread_local PER MODULE (each DLL gets its own copy of an inline
// function's static). That is what you want: put one guard at every module's emit entry point and a
// host -> core -> host loop is cut at whichever module started it.

#include <atomic>
#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

#if defined(_WIN32)
  #include <windows.h>
  #define DREAM_CALL __cdecl
#else
  #include <time.h>
  #include <unistd.h>
  #include <sys/syscall.h>
  #define DREAM_CALL
#endif

namespace dreamkurumi {

constexpr uint16_t kRawLogAbiVersion = 1;

#ifndef DREAM_LOG_LEVEL_DEFINED
#define DREAM_LOG_LEVEL_DEFINED
enum class LogLevel : uint8_t { Trace = 0, Debug = 1, Info = 2, Warn = 3, Error = 4, Fatal = 5, None = 6 };
#endif
enum RawLogFlags : uint8_t { kRawLogTruncated = 1 };

// Fixed 512 bytes, POD, no pointers: safe to copy across DLL boundaries and to memcpy into a ring.
struct DreamRawLogMessage {
    uint32_t structSize;     // sizeof(DreamRawLogMessage) of the producer (forward compat)
    uint16_t abiVersion;     // kRawLogAbiVersion
    uint8_t  level;          // LogLevel
    uint8_t  flags;          // RawLogFlags
    uint32_t threadId;       // OS thread id of the emitter
    uint32_t textLen;        // bytes in text[], excluding NUL
    uint64_t ticks;          // raw monotonic counter (QueryPerformanceCounter), see RawLogClock
    char     source[32];     // emitter id: "dinput8", "DreamAutoPickup", "core" ...
    char     text[456];      // UTF-8, NUL-terminated, NO timestamp / level tag / trailing newline
};
static_assert(sizeof(DreamRawLogMessage) == 512, "DreamRawLogMessage ABI size must stay 512 bytes");

using RawLogSinkFn = void (DREAM_CALL*)(const DreamRawLogMessage*);

// ---------------------------------------------------------------------------------------------
// Primitives
// ---------------------------------------------------------------------------------------------
inline uint64_t RawLogTicks() {
#if defined(_WIN32)
    LARGE_INTEGER t; QueryPerformanceCounter(&t); return static_cast<uint64_t>(t.QuadPart);
#else
    timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<uint64_t>(ts.tv_sec) * 1000000000ull + static_cast<uint64_t>(ts.tv_nsec);
#endif
}
inline uint64_t RawLogTicksPerSecond() {
#if defined(_WIN32)
    LARGE_INTEGER f; QueryPerformanceFrequency(&f); return static_cast<uint64_t>(f.QuadPart);
#else
    return 1000000000ull;
#endif
}
inline uint32_t RawLogThreadId() {
#if defined(_WIN32)
    return GetCurrentThreadId();
#else
    return static_cast<uint32_t>(syscall(SYS_gettid));
#endif
}

// Fills a message from already-plain text. Truncation never splits a UTF-8 sequence.
inline void RawLogFill(DreamRawLogMessage& m, LogLevel level, const char* source, const char* text, size_t len) {
    std::memset(&m, 0, sizeof(m));
    m.structSize = sizeof(m);
    m.abiVersion = kRawLogAbiVersion;
    m.level = static_cast<uint8_t>(level);
    m.threadId = RawLogThreadId();
    m.ticks = RawLogTicks();
    if (source) {
        size_t n = std::strlen(source);
        if (n > sizeof(m.source) - 1) n = sizeof(m.source) - 1;
        std::memcpy(m.source, source, n);
    }
    if (!text) return;
    const size_t cap = sizeof(m.text) - 1;
    size_t n = len;
    if (n > cap) {
        n = cap;
        m.flags |= kRawLogTruncated;
        while (n > 0 && (static_cast<unsigned char>(text[n]) & 0xC0) == 0x80) --n;   // back to a char boundary
    }
    std::memcpy(m.text, text, n);
    m.textLen = static_cast<uint32_t>(n);
}

inline void RawLogFormatV(DreamRawLogMessage& m, LogLevel level, const char* source, const char* fmt, va_list ap) {
    char buf[1024];
    int n = std::vsnprintf(buf, sizeof(buf), fmt ? fmt : "", ap);
    if (n < 0) n = 0;
    if (static_cast<size_t>(n) >= sizeof(buf)) n = static_cast<int>(sizeof(buf)) - 1;
    RawLogFill(m, level, source, buf, static_cast<size_t>(n));
}

// ---------------------------------------------------------------------------------------------
// Legacy text bridge: strips decoration that old plugins / old loader code already added, so the core
// never prints "[04:14:41.696] [04:14:41.692] [T17808] [global] [INFO ] msg".
//   Removes ANSI escapes, then leading "[hh:mm:ss.mmm]" (any number), an optional "[T<digits>]" followed by
//   exactly one channel tag, and one level tag. Any other bracket group (e.g. "[DreamAutoPickup]") is kept.
// ---------------------------------------------------------------------------------------------
namespace detail {
inline bool IsDigit(char c) { return c >= '0' && c <= '9'; }

inline bool IsTimestampToken(const char* s, size_t n) {          // dd:dd:dd or dd:dd:dd.d{1,6}
    if (n < 8 || n > 15) return false;
    for (size_t i = 0; i < 8; ++i) {
        if (i == 2 || i == 5) { if (s[i] != ':') return false; }
        else if (!IsDigit(s[i])) return false;
    }
    if (n == 8) return true;
    if (s[8] != '.') return false;
    for (size_t i = 9; i < n; ++i) if (!IsDigit(s[i])) return false;
    return n > 9;
}
inline bool IsThreadToken(const char* s, size_t n) {
    if (n < 2 || (s[0] != 'T' && s[0] != 't')) return false;
    for (size_t i = 1; i < n; ++i) if (!IsDigit(s[i])) return false;
    return true;
}
inline bool EqNoCase(const char* s, size_t n, const char* word) {
    size_t w = std::strlen(word);
    if (n != w) return false;
    for (size_t i = 0; i < n; ++i) {
        char a = s[i]; if (a >= 'a' && a <= 'z') a = static_cast<char>(a - 32);
        if (a != word[i]) return false;
    }
    return true;
}
inline bool LevelFromToken(const char* s, size_t n, LogLevel* out) {
    struct { const char* w; LogLevel l; } t[] = {
        {"TRACE", LogLevel::Trace}, {"DEBUG", LogLevel::Debug}, {"INFO", LogLevel::Info},
        {"WARN", LogLevel::Warn}, {"WARNING", LogLevel::Warn}, {"ERROR", LogLevel::Error}, {"FATAL", LogLevel::Fatal}};
    for (auto& e : t) if (EqNoCase(s, n, e.w)) { if (out) *out = e.l; return true; }
    return false;
}
} // namespace detail

// Returns bytes written to out (NUL-terminated). *level is set only if a level tag was found.
inline size_t RawLogStripLegacy(const char* in, size_t len, char* out, size_t cap, LogLevel* level, bool* foundLevel) {
    if (foundLevel) *foundLevel = false;
    if (!out || cap == 0) return 0;
    out[0] = 0;
    if (!in) return 0;

    char tmp[1024];
    size_t n = 0;
    for (size_t i = 0; i < len && n < sizeof(tmp) - 1; ++i) {            // pass 1: drop ANSI "ESC [ ... final"
        if (in[i] == '\x1b' && i + 1 < len && in[i + 1] == '[') {
            i += 2;
            while (i < len && !(static_cast<unsigned char>(in[i]) >= 0x40 && static_cast<unsigned char>(in[i]) <= 0x7E)) ++i;
            continue;
        }
        tmp[n++] = in[i];
    }

    size_t p = 0;                                                         // pass 2: leading decoration
    bool sawT = false, channelNext = false, sawLevel = false;
    for (;;) {
        while (p < n && tmp[p] == ' ') ++p;
        if (p >= n || tmp[p] != '[') break;
        size_t q = p + 1;
        while (q < n && tmp[q] != ']' && q - p <= 24) ++q;
        if (q >= n || tmp[q] != ']') break;
        const char* tok = tmp + p + 1; size_t tl = q - p - 1;
        while (tl && *tok == ' ') { ++tok; --tl; }
        while (tl && tok[tl - 1] == ' ') --tl;
        LogLevel lv{};
        if (detail::IsTimestampToken(tok, tl)) { p = q + 1; continue; }
        if (!sawT && detail::IsThreadToken(tok, tl)) { sawT = true; channelNext = true; p = q + 1; continue; }
        if (channelNext) {                                                // exactly one channel tag after [T..]
            channelNext = false;
            if (!detail::LevelFromToken(tok, tl, nullptr)) { p = q + 1; continue; }
        }
        if (!sawLevel && detail::LevelFromToken(tok, tl, &lv)) {
            sawLevel = true;
            if (level) *level = lv;
            if (foundLevel) *foundLevel = true;
            p = q + 1; continue;
        }
        break;
    }
    while (p < n && tmp[p] == ' ') ++p;
    while (n > p && (tmp[n - 1] == '\r' || tmp[n - 1] == '\n' || tmp[n - 1] == ' ')) --n;
    size_t w = n - p; if (w > cap - 1) w = cap - 1;
    std::memcpy(out, tmp + p, w);
    out[w] = 0;
    return w;
}

// ---------------------------------------------------------------------------------------------
// Per-thread, per-module re-entry guard
// ---------------------------------------------------------------------------------------------
class RawLogReentryGuard {
public:
    RawLogReentryGuard() : m_acquired(!Flag()) { if (m_acquired) Flag() = true; }
    ~RawLogReentryGuard() { if (m_acquired) Flag() = false; }
    bool Acquired() const { return m_acquired; }
    RawLogReentryGuard(const RawLogReentryGuard&) = delete;
    RawLogReentryGuard& operator=(const RawLogReentryGuard&) = delete;
private:
    static bool& Flag() { static thread_local bool s_inside = false; return s_inside; }
    bool m_acquired;
};

// ---------------------------------------------------------------------------------------------
// Ticks -> wall clock text, used by the core's sink
// ---------------------------------------------------------------------------------------------
class RawLogClock {
public:
    RawLogClock() : m_anchorTicks(RawLogTicks()), m_freq(RawLogTicksPerSecond()) {
#if defined(_WIN32)
        SYSTEMTIME st; GetLocalTime(&st);
        m_baseMs = ((static_cast<int64_t>(st.wHour) * 60 + st.wMinute) * 60 + st.wSecond) * 1000 + st.wMilliseconds;
#else
        timespec ts; clock_gettime(CLOCK_REALTIME, &ts);
        tm lt; localtime_r(&ts.tv_sec, &lt);
        m_baseMs = ((static_cast<int64_t>(lt.tm_hour) * 60 + lt.tm_min) * 60 + lt.tm_sec) * 1000 + ts.tv_nsec / 1000000;
#endif
    }
    // out must hold at least 16 bytes: "HH:MM:SS.mmm"
    void Format(uint64_t ticks, char* out, size_t cap) const {
        const int64_t d = static_cast<int64_t>(ticks - m_anchorTicks);          // negative for boot-time messages
        int64_t ms = m_baseMs + static_cast<int64_t>(static_cast<double>(d) * 1000.0 / static_cast<double>(m_freq));
        constexpr int64_t kDay = 86400000;
        ms = ((ms % kDay) + kDay) % kDay;
        std::snprintf(out, cap, "%02d:%02d:%02d.%03d", static_cast<int>(ms / 3600000), static_cast<int>(ms / 60000 % 60),
                      static_cast<int>(ms / 1000 % 60), static_cast<int>(ms % 1000));
    }
private:
    uint64_t m_anchorTicks; uint64_t m_freq; int64_t m_baseMs;
};

// ---------------------------------------------------------------------------------------------
// DreamBootLog: lives in dinput8. Lock-free, allocation-free, one-shot early buffer.
// ---------------------------------------------------------------------------------------------
class DreamBootLog {
public:
    static constexpr uint32_t kCapacity = 256;

    static DreamBootLog& Instance() { static DreamBootLog s; return s; }

    // Any thread, any time (including DllMain).
    void Emit(const DreamRawLogMessage& m) {
        RawLogReentryGuard guard;
        if (!guard.Acquired()) { m_reentrant.fetch_add(1, std::memory_order_relaxed); return; }
        if (RawLogSinkFn s = m_sink.load(std::memory_order_acquire)) { s(&m); return; }
        Push(m);
    }

    void Log(LogLevel level, const char* source, const char* fmt, ...) {
        DreamRawLogMessage m;
        va_list ap; va_start(ap, fmt);
        RawLogFormatV(m, level, source, fmt, ap);
        va_end(ap);
        Emit(m);
    }

    // For old code that still hands over a finished line (s_pfnHostLog(buf)). Strips old decoration first.
    void EmitLegacyText(LogLevel defaultLevel, const char* source, const char* text) {
        if (!text) return;
        char clean[512]; LogLevel lv = defaultLevel; bool found = false;
        size_t n = RawLogStripLegacy(text, std::strlen(text), clean, sizeof(clean), &lv, &found);
        DreamRawLogMessage m;
        RawLogFill(m, found ? lv : defaultLevel, source, clean, n);
        Emit(m);
    }

    // Core -> host. Delivers buffered messages in order, then switches to direct delivery.
    // Ordering across threads is best-effort in the switch window; every message keeps its own tick.
    bool AttachSink(RawLogSinkFn sink) {
        if (!sink) return false;
        RawLogSinkFn expected = nullptr;
        if (m_consumed.load(std::memory_order_acquire)) {             // re-attach (e.g. core reload): nothing to replay
            return m_sink.compare_exchange_strong(expected, sink, std::memory_order_acq_rel) || expected == sink;
        }
        RawLogReentryGuard guard;                                     // a sink that logs back while flushing is cut off
        // Flush once before going live so earlier messages come first...
        uint32_t next = 0;
        FlushFrom(next, sink);
        if (!m_sink.compare_exchange_strong(expected, sink, std::memory_order_acq_rel)) return expected == sink;
        // ...and once more for messages that were buffered while we were flushing.
        FlushFrom(next, sink);
        m_consumed.store(true, std::memory_order_release);
        const uint32_t dropped = m_dropped.load(std::memory_order_relaxed);
        if (dropped) {
            DreamRawLogMessage n;
            char t[96]; int w = std::snprintf(t, sizeof(t), "%u early-boot log message(s) were dropped (buffer full)", dropped);
            RawLogFill(n, LogLevel::Warn, "dinput8", t, w > 0 ? static_cast<size_t>(w) : 0);
            sink(&n);
        }
        return true;
    }
    void DetachSink(RawLogSinkFn sink) {
        RawLogSinkFn cur = sink;
        m_sink.compare_exchange_strong(cur, nullptr, std::memory_order_acq_rel);
    }
    uint32_t Dropped() const { return m_dropped.load(std::memory_order_relaxed); }
    uint32_t ReentrantDropped() const { return m_reentrant.load(std::memory_order_relaxed); }
    uint32_t Buffered() const {
        const uint32_t h = m_head.load(std::memory_order_relaxed);
        return h < kCapacity ? h : kCapacity;
    }

private:
    struct Slot { std::atomic<uint32_t> state{0}; DreamRawLogMessage msg; };   // 0 empty, 1 ready

    void Push(const DreamRawLogMessage& m) {
        if (m_consumed.load(std::memory_order_acquire)) { Fallback(m); return; }   // sink was detached later
        const uint32_t i = m_head.fetch_add(1, std::memory_order_relaxed);
        if (i >= kCapacity) { m_dropped.fetch_add(1, std::memory_order_relaxed); return; }
        m_slots[i].msg = m;
        m_slots[i].state.store(1, std::memory_order_release);
    }

    void FlushFrom(uint32_t& next, RawLogSinkFn sink) {
        uint32_t end = m_head.load(std::memory_order_acquire);
        if (end > kCapacity) end = kCapacity;
        for (; next < end; ++next) {
            for (int spin = 0; spin < 2000 && m_slots[next].state.load(std::memory_order_acquire) != 1; ++spin) {}
            if (m_slots[next].state.load(std::memory_order_acquire) == 1) sink(&m_slots[next].msg);
        }
    }

    static void Fallback(const DreamRawLogMessage& m) {
#if defined(_WIN32)
        OutputDebugStringA(m.text); OutputDebugStringA("\n");
#else
        std::fprintf(stderr, "%s\n", m.text);
#endif
    }

    Slot m_slots[kCapacity];
    std::atomic<uint32_t> m_head{0};
    std::atomic<uint32_t> m_dropped{0};
    std::atomic<uint32_t> m_reentrant{0};
    std::atomic<bool> m_consumed{false};
    std::atomic<RawLogSinkFn> m_sink{nullptr};
};

// ---------------------------------------------------------------------------------------------
// C-ABI table the host hands to the core (add a pointer field to your existing host context).
// ---------------------------------------------------------------------------------------------
struct DreamLogBridgeV1 {
    uint32_t structSize;
    uint16_t abiVersion;
    uint16_t reserved;
    void     (DREAM_CALL* emit)(const DreamRawLogMessage*);
    bool     (DREAM_CALL* attachSink)(RawLogSinkFn);
    void     (DREAM_CALL* detachSink)(RawLogSinkFn);
    uint32_t (DREAM_CALL* droppedCount)();
};

inline const DreamLogBridgeV1* DreamBootLog_GetBridge() {            // call in dinput8; pass the pointer to the core
    static const DreamLogBridgeV1 bridge = {
        sizeof(DreamLogBridgeV1), kRawLogAbiVersion, 0,
        [](const DreamRawLogMessage* m) { if (m) DreamBootLog::Instance().Emit(*m); },
        [](RawLogSinkFn s) { return DreamBootLog::Instance().AttachSink(s); },
        [](RawLogSinkFn s) { DreamBootLog::Instance().DetachSink(s); },
        []() { return DreamBootLog::Instance().Dropped(); }};
    return &bridge;
}

// ---------------------------------------------------------------------------------------------
// Core-side sketch (not compiled here):
//
//   static RawLogClock g_clock;
//   static void DREAM_CALL CoreSink(const DreamRawLogMessage* m) {       // runs on the emitting thread
//       g_ring.TryPush(*m);                                              // memcpy 512 B into the async ring
//   }                                                                    // formatting happens on the log thread:
//   // log thread: g_clock.Format(m.ticks, ts, sizeof ts); print "[ts] [T<tid>] [source] [LEVEL] text"
//   // init:       bridge->attachSink(&CoreSink);   shutdown/hot-reload: bridge->detachSink(&CoreSink);
//   // core's own LogInfo() builds a DreamRawLogMessage and calls the same ring directly (guarded by its own
//   // RawLogReentryGuard), and ctx->logCallback for plugins should point at bridge->emit.
// ---------------------------------------------------------------------------------------------

} // namespace dreamkurumi
