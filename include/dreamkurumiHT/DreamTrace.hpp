#pragma once
// DreamTrace.hpp - lightweight call-sequence tracing harness for dreamkurumiHT
//
// Goal: record WHICH functions run (and who called them) during two experiments,
// then diff them offline (trace_diff.py), e.g.
//   Phase A: mine a voxel normally            -> loot spawns
//   Phase B: mod sets TemporaryMining.relativeHealth = 0 -> does loot spawn?
//
// Design rules:
//   * Hot path (Record) does NO allocation, NO logging, NO locks: fixed ring buffer + seqlock.
//   * All symbolization / file IO happens in DumpJsonl(), on a thread you choose, never in a hook.
//   * Stack walk uses RtlVirtualUnwind starting from the *original* RSP (ctx + sizeof(DreamCpuContext)),
//     because the midhook stub has no UNWIND_INFO and RtlCaptureStackBackTrace would stop at it.

#include <windows.h>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "DreamHook.hpp"        // DreamCpuContext, DreamMidHook
#include "DreamCrashGuard.hpp"  // DreamCrashGuard::ExecuteSafely

namespace dreamkurumi::trace {

constexpr size_t kCapacity  = 1u << 16;  // events in ring (power of two); ~10 MB
constexpr size_t kMaxFrames = 12;
constexpr size_t kMaxHooks  = 256;

enum class Kind : uint8_t { Call = 1, Marker = 2 };

struct EventData {
    uint64_t seq = 0;
    uint64_t qpc = 0;
    uint64_t args[4] = {};            // rcx, rdx, r8, r9
    uint64_t frames[kMaxFrames] = {}; // [0] = hooked function, [1..] = callers
    float    xmm0 = 0.f;              // first float arg (health/damage values show up here)
    uint32_t tid = 0;
    uint16_t id = 0;                  // hook id (Call) or label index (Marker)
    Kind     kind = Kind::Call;
    uint8_t  nframes = 0;
};

struct Event {
    std::atomic<uint64_t> commit{0};  // == seq+1 when fully written (seqlock)
    EventData d;
};

class Collector {
public:
    static Collector& Instance() { static Collector c; return c; }

    // ---- cold path -------------------------------------------------------------------------
    uint16_t RegisterHook(const char* name, void* addr) {
        std::lock_guard<std::mutex> g(m_cfg);
        uint32_t i = m_hookCount.load(std::memory_order_relaxed);
        if (i >= kMaxHooks) return 0xFFFF;
        std::strncpy(m_hooks[i].name, name, sizeof(m_hooks[i].name) - 1);
        m_hooks[i].addr = reinterpret_cast<uint64_t>(addr);
        m_hookCount.store(i + 1, std::memory_order_release);
        return static_cast<uint16_t>(i);
    }

    void Arm(bool on) { m_armed.store(on, std::memory_order_release); }

    // Begin a labelled phase: writes a marker into the stream and arms recording.
    void BeginPhase(const char* label) { Marker(label); Arm(true); }
    void EndPhase(const char* label)   { Arm(false); Marker(label); }

    void Marker(const char* label) {
        uint16_t li;
        {
            std::lock_guard<std::mutex> g(m_cfg);
            li = static_cast<uint16_t>(m_labels.size());
            m_labels.emplace_back(label);
        }
        EventData d;
        d.kind = Kind::Marker;
        d.id = li;
        d.tid = GetCurrentThreadId();
        LARGE_INTEGER t; QueryPerformanceCounter(&t);
        d.qpc = static_cast<uint64_t>(t.QuadPart);
        Emit(d);
    }

    // ---- hot path --------------------------------------------------------------------------
    // From a MidHook callback.
    void Record(uint16_t id, const DreamCpuContext* c) {
        if (!m_armed.load(std::memory_order_relaxed) || id >= kMaxHooks) return;
        CONTEXT cx{};
        cx.ContextFlags = CONTEXT_CONTROL | CONTEXT_INTEGER;
        cx.Rip = m_hooks[id].addr;
        cx.Rsp = reinterpret_cast<DWORD64>(c) + sizeof(DreamCpuContext);  // 0x280 (exact RSP before stub)
        cx.Rbx = c->rbx; cx.Rbp = c->rbp; cx.Rdi = c->rdi; cx.Rsi = c->rsi;
        cx.R12 = c->r12; cx.R13 = c->r13; cx.R14 = c->r14; cx.R15 = c->r15;
        const uint64_t args[4] = { c->rcx, c->rdx, c->r8, c->r9 };
        RecordCommon(id, cx, args, c->xmm[0].f32[0]);
    }

    // From a VEH hardware-breakpoint handler (e.g. "who writes this voxel's health?").
    // Use the CONTEXT the VEH received; the RIP recorded is the instruction AFTER the write (trap semantics).
    void RecordWin(uint16_t id, const CONTEXT* w) {
        if (!m_armed.load(std::memory_order_relaxed)) return;
        CONTEXT cx = *w;
        const uint64_t args[4] = { w->Rcx, w->Rdx, w->R8, w->R9 };
        float x0; std::memcpy(&x0, &w->Xmm0, sizeof(x0));
        RecordCommon(id, cx, args, x0);
    }

    // ---- dump (call from a non-hooked thread, e.g. hotkey or your scheduler) ----------------
    bool DumpJsonl(const char* path) {
        FILE* f = std::fopen(path, "wb");
        if (!f) return false;
        LARGE_INTEGER fq; QueryPerformanceFrequency(&fq);
        const uint64_t head  = m_head.load(std::memory_order_acquire);
        const uint64_t first = head > kCapacity ? head - kCapacity : 0;
        uint64_t t0 = 0; bool haveT0 = false;
        std::vector<std::string> labels; { std::lock_guard<std::mutex> g(m_cfg); labels = m_labels; }
        const uint32_t nHooks = m_hookCount.load(std::memory_order_acquire);

        for (uint64_t seq = first; seq < head; ++seq) {
            Event& e = m_buf[seq & (kCapacity - 1)];
            const uint64_t c1 = e.commit.load(std::memory_order_acquire);
            if (c1 != seq + 1) continue;                       // overwritten or still being written
            EventData d = e.d;
            std::atomic_thread_fence(std::memory_order_acquire);
            if (e.commit.load(std::memory_order_relaxed) != c1) continue;

            if (!haveT0) { t0 = d.qpc; haveT0 = true; }
            const double us = double(d.qpc - t0) * 1e6 / double(fq.QuadPart);

            if (d.kind == Kind::Marker) {
                std::string l = d.id < labels.size() ? labels[d.id] : "?";
                for (char& ch : l) if (ch == '"' || ch == '\\') ch = '_';
                std::fprintf(f, "{\"seq\":%llu,\"t_us\":%.1f,\"marker\":\"%s\"}\n",
                             (unsigned long long)d.seq, us, l.c_str());
                continue;
            }
            const char* hn = d.id < nHooks ? m_hooks[d.id].name : "?";
            std::fprintf(f, "{\"seq\":%llu,\"t_us\":%.1f,\"tid\":%u,\"hook\":\"%s\",\"xmm0\":%g,\"args\":[",
                         (unsigned long long)d.seq, us, d.tid, hn, (double)d.xmm0);
            for (int i = 0; i < 4; ++i)
                std::fprintf(f, "%s\"0x%llx\"", i ? "," : "", (unsigned long long)d.args[i]);
            std::fprintf(f, "],\"frames\":[");
            for (uint8_t i = 0; i < d.nframes; ++i)
                std::fprintf(f, "%s\"%s\"", i ? "," : "", Sym(d.frames[i]).c_str());
            std::fprintf(f, "]}\n");
        }
        std::fclose(f);
        return true;
    }

private:
    struct HookInfo { char name[64] = {}; uint64_t addr = 0; };

    Collector() : m_buf(new Event[kCapacity]) {}

    void Emit(const EventData& src) {
        const uint64_t seq = m_head.fetch_add(1, std::memory_order_relaxed);
        Event& e = m_buf[seq & (kCapacity - 1)];
        e.commit.store(0, std::memory_order_relaxed);
        std::atomic_thread_fence(std::memory_order_release);
        e.d = src;
        e.d.seq = seq;
        e.commit.store(seq + 1, std::memory_order_release);
    }

    void RecordCommon(uint16_t id, CONTEXT cx, const uint64_t (&args)[4], float xmm0) {
        static thread_local bool busy = false;   // never trace our own work re-entrantly
        if (busy) return;
        busy = true;
        EventData d;
        d.kind = Kind::Call; d.id = id; d.tid = GetCurrentThreadId(); d.xmm0 = xmm0;
        std::memcpy(d.args, args, sizeof(d.args));
        LARGE_INTEGER t; QueryPerformanceCounter(&t);
        d.qpc = static_cast<uint64_t>(t.QuadPart);
        // Unwinding reads game stack memory; guard it so a bad frame can't kill the game.
        dreamkurumi::DreamCrashGuard::ExecuteSafely("DreamTrace", [&]() {
            d.nframes = CaptureFrames(cx, d.frames, kMaxFrames);
        });
        Emit(d);
        busy = false;
    }

    static uint8_t CaptureFrames(CONTEXT cx, uint64_t* out, size_t max) {
        size_t n = 0;
        while (n < max && cx.Rip > 0x10000) {
            out[n++] = cx.Rip;
            DWORD64 base = 0;
            PRUNTIME_FUNCTION fe = RtlLookupFunctionEntry(cx.Rip, &base, nullptr);
            if (fe) {
                PVOID handlerData = nullptr; DWORD64 establisher = 0;
                RtlVirtualUnwind(UNW_FLAG_NHANDLER, base, cx.Rip, fe, &cx, &handlerData, &establisher, nullptr);
            } else {                                   // leaf function without .pdata entry
                cx.Rip = *reinterpret_cast<DWORD64*>(cx.Rsp);
                cx.Rsp += 8;
            }
        }
        return static_cast<uint8_t>(n);
    }

    std::string Sym(uint64_t a) {
        HMODULE m = nullptr;
        char b[160];
        if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                reinterpret_cast<LPCSTR>(a), &m) || !m) {
            std::snprintf(b, sizeof b, "?+0x%llx", (unsigned long long)a);
            return b;
        }
        auto it = m_modNames.find(reinterpret_cast<uintptr_t>(m));
        if (it == m_modNames.end()) {
            char p[MAX_PATH] = {};
            GetModuleFileNameA(m, p, MAX_PATH);
            const char* s = std::strrchr(p, '\\');
            it = m_modNames.emplace(reinterpret_cast<uintptr_t>(m), std::string(s ? s + 1 : p)).first;
        }
        std::snprintf(b, sizeof b, "%s+0x%llx", it->second.c_str(),
                      (unsigned long long)(a - reinterpret_cast<uintptr_t>(m)));
        return b;
    }

    std::unique_ptr<Event[]> m_buf;
    std::atomic<uint64_t> m_head{0};
    std::atomic<bool>     m_armed{false};
    std::atomic<uint32_t> m_hookCount{0};
    HookInfo              m_hooks[kMaxHooks];
    std::mutex            m_cfg;
    std::vector<std::string> m_labels;
    std::unordered_map<uintptr_t, std::string> m_modNames;  // only touched in DumpJsonl
};

// ---- glue to MidHook --------------------------------------------------------------------
inline void TraceCallback(dreamkurumi::DreamCpuContext& ctx, void* user) {
    Collector::Instance().Record(static_cast<uint16_t>(reinterpret_cast<uintptr_t>(user)), &ctx);
}

inline void TraceCallbackPtr(dreamkurumi::DreamCpuContext* ctx, void* user) {
    if (ctx) Collector::Instance().Record(static_cast<uint16_t>(reinterpret_cast<uintptr_t>(user)), ctx);
}

inline std::vector<std::unique_ptr<dreamkurumi::DreamMidHook>>& GetActiveTraceHooks() {
    static std::vector<std::unique_ptr<dreamkurumi::DreamMidHook>> s_hooks;
    return s_hooks;
}

inline bool TraceFunction(const char* name, void* target) {
    const uint16_t id = Collector::Instance().RegisterHook(name, target);
    if (id == 0xFFFF) return false;
    auto hook = std::make_unique<dreamkurumi::DreamMidHook>();
    if (!hook->Install(target, &TraceCallback, reinterpret_cast<void*>(static_cast<uintptr_t>(id)))) {
        return false;
    }
    GetActiveTraceHooks().push_back(std::move(hook));
    return true;
}

using InstallMidHookFn = bool (*)(void* target, void (*cb)(dreamkurumi::DreamCpuContext*, void*), void* user);

inline bool TraceFunctionCustom(InstallMidHookFn install, const char* name, void* target) {
    const uint16_t id = Collector::Instance().RegisterHook(name, target);
    if (id == 0xFFFF) return false;
    return install(target, &TraceCallbackPtr, reinterpret_cast<void*>(static_cast<uintptr_t>(id)));
}

} // namespace dreamkurumi::trace

namespace dream {
    namespace trace = dreamkurumi::trace;
}
