#pragma once
// DreamEventContext.hpp - Gate -> Action -> Observe chain for hooks that several mods share
//
//   Gate    (may Cancel)   : guard conditions. Ordered; the first Cancel() wins, remaining Gates and ALL Actions
//                            are skipped.
//   Action  (may Claim)    : gameplay reactions. Ordered; the first Claim() takes the event, later Actions are
//                            skipped unless they opted in with runEvenIfClaimed.
//   Observe (read-only)    : telemetry. ALWAYS runs, also for canceled or claimed events, and receives a const
//                            view, so it cannot change the outcome.
//
// Ordering inside a stage (deterministic, independent of load order):
//   effective priority DESC  (base priority + PriorityOverrides[modId]),  then modId ASC (ASCII, case-insensitive),
//   then handler name ASC.  (stage, modId, name) is unique: registering the same key again replaces it.
//
// Synthetic events: when a mod makes the game fire an event itself (AoE harvest -> interaction_attempt), wrap the call
//   in  event::SyntheticScope scope("DreamAoEHarvest");  The dispatcher then sets isSyntheticEvent + syntheticOrigin.
//   Defaults: Gate and Observe receive synthetic events, Action does not (opt in per handler). Nesting deeper than
//   kMaxSyntheticDepth cancels the event (loop breaker).
//
// Safety: handlers run through DREAM_EVENT_INVOKE (define it before including to use DreamCrashGuard):
//     #define DREAM_EVENT_INVOKE(mod, fn) ::dreamkurumi::DreamCrashGuard::ExecuteSafely(mod, fn)
//   A handler that faults kFaultLimit times in a row is disabled (per handler, not per name). A Gate registered with
//   failClosed=true cancels the event when it faults; otherwise a faulting Gate is ignored (fail-open).
//
// Hot reload: RemoveByMod() + WaitForQuiescence() guarantee that no dispatch that could still call into the old module
//   is running before you FreeLibrary it. A dispatch keeps its snapshot (and the std::function objects inside it) alive
//   until it returns, and the in-flight counter drops only after the snapshot is released.
//
// ABI caveats (important with several DLLs):
//   * EventChain objects must live in ONE module (the core); mods register through it. Handlers are std::function, so
//     all modules must be built with the same compiler / STL / CRT settings.
//   * The synthetic-event thread state must also be shared. Core exports its accessor (event::LocalSynthetic) via the
//     plugin context; each mod calls event::BindSyntheticAccessor(ctx->getSyntheticState) once at init. Without that,
//     a mod's SyntheticScope would write to the mod's own thread_local copy and the core would never see it.

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>
#if __has_include(<version>)
  #include <version>
#endif

#ifndef DREAM_EVENT_INVOKE
#define DREAM_EVENT_INVOKE(modId, callable) ::dreamkurumi::event::DefaultInvoke(callable)
#endif

namespace dreamkurumi::event {

template <class F> inline bool DefaultInvoke(F&& f) {
    try { f(); return true; } catch (...) { return false; }
}

enum class Stage : uint8_t { Gate = 0, Action = 1, Observe = 2 };
enum class SyntheticPolicy : uint8_t { Default, Accept, Skip };

constexpr int      kMaxSyntheticDepth = 4;
constexpr uint32_t kFaultLimit = 5;

// ---------------------------------------------------------------------------------------------
// Synthetic-event thread state (shared through an accessor, see ABI caveats)
// ---------------------------------------------------------------------------------------------
struct SyntheticState { int depth = 0; const char* origin = nullptr; };
using SyntheticAccessorFn = SyntheticState* (*)();

inline SyntheticState* LocalSynthetic() { static thread_local SyntheticState s; return &s; }
inline SyntheticAccessorFn& AccessorSlot() { static SyntheticAccessorFn f = &LocalSynthetic; return f; }
inline void BindSyntheticAccessor(SyntheticAccessorFn f) { AccessorSlot() = f ? f : &LocalSynthetic; }
inline SyntheticState& Synthetic() { return *AccessorSlot()(); }

class SyntheticScope {
public:
    explicit SyntheticScope(const char* originModId) : m_s(Synthetic()), m_prevOrigin(m_s.origin) {
        if (m_s.depth == 0 || !m_s.origin) m_s.origin = originModId;      // keep the OUTERMOST origin
        ++m_s.depth;
    }
    ~SyntheticScope() { --m_s.depth; m_s.origin = m_prevOrigin; }
    SyntheticScope(const SyntheticScope&) = delete;
    SyntheticScope& operator=(const SyntheticScope&) = delete;
private:
    SyntheticState& m_s;
    const char* m_prevOrigin;
};

// ---------------------------------------------------------------------------------------------
// Priority overrides (config: [hook_priority] "DreamAutoPickup" = 10). Core-owned.
// ---------------------------------------------------------------------------------------------
namespace detail {
inline std::string LowerAscii(std::string s) {
    for (char& c : s) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c + 32);
    return s;
}
inline void CopyStr(char* dst, size_t cap, const char* src) {
    if (!dst || cap == 0) return;
    if (!src) { dst[0] = 0; return; }
    size_t n = std::strlen(src); if (n > cap - 1) n = cap - 1;
    std::memcpy(dst, src, n); dst[n] = 0;
}
template <class T> class AtomicSp {
#if defined(__cpp_lib_atomic_shared_ptr)
public:
    std::shared_ptr<const T> load() const { return m_p.load(std::memory_order_acquire); }
    void store(std::shared_ptr<const T> v) { m_p.store(std::move(v), std::memory_order_release); }
private:
    std::atomic<std::shared_ptr<const T>> m_p;
#else
public:
    std::shared_ptr<const T> load() const { std::lock_guard<std::mutex> g(m_m); return m_p; }
    void store(std::shared_ptr<const T> v) { std::lock_guard<std::mutex> g(m_m); m_p = std::move(v); }
private:
    mutable std::mutex m_m; std::shared_ptr<const T> m_p;
#endif
};
} // namespace detail

class PriorityOverrides {
public:
    static PriorityOverrides& Instance() { static PriorityOverrides p; return p; }
    void Set(const std::string& modId, int delta) {
        { std::lock_guard<std::mutex> g(m_mtx); m_map[detail::LowerAscii(modId)] = delta; }
        m_version.fetch_add(1, std::memory_order_release);
    }
    void Clear() { { std::lock_guard<std::mutex> g(m_mtx); m_map.clear(); } m_version.fetch_add(1, std::memory_order_release); }
    int Get(const std::string& modId) const {
        std::lock_guard<std::mutex> g(m_mtx);
        auto it = m_map.find(detail::LowerAscii(modId));
        return it == m_map.end() ? 0 : it->second;
    }
    uint64_t Version() const { return m_version.load(std::memory_order_acquire); }
private:
    mutable std::mutex m_mtx;
    std::unordered_map<std::string, int> m_map;
    std::atomic<uint64_t> m_version{1};
};

// ---------------------------------------------------------------------------------------------
// Contexts
// ---------------------------------------------------------------------------------------------
struct HandlerOptions {
    int  priority = 0;                                   // higher runs earlier inside its stage
    SyntheticPolicy synthetic = SyntheticPolicy::Default; // Default: Gate/Observe accept, Action skips
    bool runEvenIfClaimed = false;                       // Action only
    bool failClosed = false;                             // Gate only: a fault cancels the event
    const char* name = "";                               // distinguishes several handlers of one mod
};

template <class P> class EventContext {
public:
    EventContext(P& payload, uint64_t id, bool synth, const char* origin, int depth)
        : data(payload), eventId(id), isSyntheticEvent(synth), syntheticOrigin(origin), syntheticDepth(depth) {}

    P& data;                          // mutable payload (Gate / Action)
    const uint64_t eventId;
    Stage stage = Stage::Gate;        // set by the dispatcher
    const bool isSyntheticEvent;
    const char* const syntheticOrigin;
    const int syntheticDepth;
    const char* currentModId = "";    // the handler being called

    bool canceled() const { return m_canceled; }
    const char* canceledBy() const { return m_canceledBy; }
    const char* cancelReason() const { return m_reason; }
    bool claimed() const { return m_claimed; }
    const char* claimedBy() const { return m_claimedBy; }
    uint32_t misuseCount() const { return m_misuse; }

    void Cancel(const char* reason = nullptr) {                         // Gate only
        if (stage != Stage::Gate) { ++m_misuse; return; }
        if (m_canceled) return;
        m_canceled = true; m_canceledBy = currentModId; m_reason = reason;
    }
    bool Claim() {                                                      // Action only; true if THIS call took it
        if (stage != Stage::Action) { ++m_misuse; return false; }
        if (m_claimed) return false;
        m_claimed = true; m_claimedBy = currentModId;
        return true;
    }
private:
    bool m_canceled = false, m_claimed = false;
    const char* m_canceledBy = nullptr; const char* m_reason = nullptr; const char* m_claimedBy = nullptr;
    uint32_t m_misuse = 0;
};

template <class P> struct ObserveContext {              // everything const: observers cannot change the outcome
    const P& data;
    uint64_t eventId;
    bool isSyntheticEvent;
    const char* syntheticOrigin;
    int syntheticDepth;
    bool canceled; const char* canceledBy; const char* cancelReason;
    bool claimed;  const char* claimedBy;
    const char* currentModId;
};

struct DispatchResult {
    bool canceled = false, claimed = false, synthetic = false;
    uint32_t handlersRun = 0, faults = 0;
    char canceledBy[48] = {}; char claimedBy[48] = {}; char reason[64] = {};
};

// ---------------------------------------------------------------------------------------------
// The chain
// ---------------------------------------------------------------------------------------------
template <class P> class EventChain {
public:
    using ActionFn = std::function<void(EventContext<P>&)>;     // used for Gate and Action
    using ObserveFn = std::function<void(const ObserveContext<P>&)>;

    EventChain(const char* name, uint64_t eventId) : m_name(name ? name : ""), m_id(eventId) {
        m_snap.store(std::make_shared<const Snapshot>());
    }

    bool RegisterGate(const std::string& modId, ActionFn fn, const HandlerOptions& o = {})    { return Register(Stage::Gate, modId, std::move(fn), nullptr, o); }
    bool RegisterAction(const std::string& modId, ActionFn fn, const HandlerOptions& o = {})  { return Register(Stage::Action, modId, std::move(fn), nullptr, o); }
    bool RegisterObserve(const std::string& modId, ObserveFn fn, const HandlerOptions& o = {}) { return Register(Stage::Observe, modId, nullptr, std::move(fn), o); }

    size_t RemoveByMod(const std::string& modId) {
        std::lock_guard<std::mutex> g(m_mtx);
        const size_t before = m_entries.size();
        m_entries.erase(std::remove_if(m_entries.begin(), m_entries.end(), [&](const Entry& e) { return e.modId == modId; }), m_entries.end());
        RebuildLocked();
        return before - m_entries.size();
    }

    // Wait until no dispatch of this chain is running. Returns false on timeout, or immediately if called from inside
    // a handler of this chain (it would wait for itself).
    bool WaitForQuiescence(uint32_t timeoutMs) const {
        if (TlsDepth() > 0) return false;
        const auto t0 = std::chrono::steady_clock::now();
        while (m_inflight.load(std::memory_order_acquire) != 0) {
            if (std::chrono::steady_clock::now() - t0 > std::chrono::milliseconds(timeoutMs)) return false;
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        return true;
    }

    void Rebuild() { std::lock_guard<std::mutex> g(m_mtx); RebuildLocked(); }
    const std::string& Name() const { return m_name; }

    size_t HandlerCount() const { std::lock_guard<std::mutex> g(m_mtx); return m_entries.size(); }

    // Execution order for diagnostics / the F9 menu.
    std::vector<std::string> Describe() const {
        auto s = LoadSnapshot();
        std::vector<std::string> out;
        auto add = [&](const char* st, const std::vector<Entry>& v) {
            for (auto& e : v) {
                std::string line = std::string(st) + " " + std::to_string(e.effPriority) + " " + e.modId;
                if (!e.name.empty()) line += "/" + e.name;
                if (e.state->disabled.load()) line += " [disabled]";
                out.push_back(std::move(line));
            }
        };
        add("Gate", s->gates); add("Action", s->actions); add("Observe", s->observes);
        return out;
    }

    DispatchResult Dispatch(P& payload) {
        InflightGuard inflight(m_inflight);                   // declared first => destroyed LAST (after snapshot release)
        DepthGuard depthGuard;
        std::shared_ptr<const Snapshot> snap = LoadSnapshot();
        if (snap->overrideVersion != PriorityOverrides::Instance().Version()) { Rebuild(); snap = LoadSnapshot(); }

        const SyntheticState& syn = Synthetic();
        const bool synth = syn.depth > 0;
        EventContext<P> ctx(payload, m_id, synth, syn.origin, syn.depth);
        DispatchResult res;
        res.synthetic = synth;

        const bool tooDeep = syn.depth > kMaxSyntheticDepth;
        if (tooDeep) { ctx.stage = Stage::Gate; ctx.currentModId = "dispatcher"; ctx.Cancel("synthetic recursion limit"); }

        // 1) Gates
        ctx.stage = Stage::Gate;
        if (!tooDeep) for (const Entry& e : snap->gates) {
            if (Skip(e, synth)) continue;
            ctx.currentModId = e.modId.c_str();
            const bool ok = Invoke(e, [&] { e.fn(ctx); });
            ++res.handlersRun;
            if (!ok) { ++res.faults; if (e.failClosed) ctx.Cancel("gate fault (fail-closed)"); }
            if (ctx.canceled()) break;
        }
        // 2) Actions
        ctx.stage = Stage::Action;
        if (!ctx.canceled()) for (const Entry& e : snap->actions) {
            if (Skip(e, synth)) continue;
            if (ctx.claimed() && !e.runEvenIfClaimed) continue;
            ctx.currentModId = e.modId.c_str();
            const bool ok = Invoke(e, [&] { e.fn(ctx); });
            ++res.handlersRun;
            if (!ok) ++res.faults;
        }
        // 3) Observers: always, read-only
        ctx.stage = Stage::Observe;
        for (const Entry& e : snap->observes) {
            if (Skip(e, synth)) continue;
            ObserveContext<P> oc{payload, m_id, synth, syn.origin, syn.depth, ctx.canceled(), ctx.canceledBy(), ctx.cancelReason(),
                                 ctx.claimed(), ctx.claimedBy(), e.modId.c_str()};
            const bool ok = Invoke(e, [&] { e.ofn(oc); });
            ++res.handlersRun;
            if (!ok) ++res.faults;
        }

        res.canceled = ctx.canceled(); res.claimed = ctx.claimed();
        detail::CopyStr(res.canceledBy, sizeof(res.canceledBy), ctx.canceledBy());
        detail::CopyStr(res.claimedBy, sizeof(res.claimedBy), ctx.claimedBy());
        detail::CopyStr(res.reason, sizeof(res.reason), ctx.cancelReason());
        return res;                                           // snapshot released here, inflight afterwards
    }

private:
    struct HandlerState { std::atomic<uint32_t> faults{0}; std::atomic<bool> disabled{false}; };
    struct Entry {
        Stage stage{}; std::string modId, modKey, name;
        int basePriority = 0, effPriority = 0;
        SyntheticPolicy synthetic = SyntheticPolicy::Default;
        bool runEvenIfClaimed = false, failClosed = false;
        ActionFn fn; ObserveFn ofn;
        std::shared_ptr<HandlerState> state;
    };
    struct Snapshot { std::vector<Entry> gates, actions, observes; uint64_t overrideVersion = 0; };

    struct InflightGuard {
        explicit InflightGuard(std::atomic<int32_t>& c) : m_c(c) { m_c.fetch_add(1, std::memory_order_acq_rel); }
        ~InflightGuard() { m_c.fetch_sub(1, std::memory_order_acq_rel); }
        std::atomic<int32_t>& m_c;
    };
    struct DepthGuard { DepthGuard() { ++TlsDepth(); } ~DepthGuard() { --TlsDepth(); } };
    static int& TlsDepth() { static thread_local int d = 0; return d; }

    std::shared_ptr<const Snapshot> LoadSnapshot() const { return m_snap.load(); }

    static bool Skip(const Entry& e, bool synth) {
        if (e.state->disabled.load(std::memory_order_relaxed)) return true;
        if (!synth) return false;
        SyntheticPolicy p = e.synthetic;
        if (p == SyntheticPolicy::Default) p = (e.stage == Stage::Action) ? SyntheticPolicy::Skip : SyntheticPolicy::Accept;
        return p == SyntheticPolicy::Skip;
    }

    template <class F> bool Invoke(const Entry& e, F&& call) {
        const bool ok = DREAM_EVENT_INVOKE(e.modId.c_str(), call);
        if (ok) { e.state->faults.store(0, std::memory_order_relaxed); return true; }
        if (e.state->faults.fetch_add(1, std::memory_order_relaxed) + 1 >= kFaultLimit)
            e.state->disabled.store(true, std::memory_order_relaxed);
        return false;
    }

    bool Register(Stage st, const std::string& modId, ActionFn fn, ObserveFn ofn, const HandlerOptions& o) {
        if (modId.empty()) return false;
        if (st == Stage::Observe ? !ofn : !fn) return false;
        Entry e;
        e.stage = st; e.modId = modId; e.modKey = detail::LowerAscii(modId); e.name = o.name ? o.name : "";
        e.basePriority = o.priority; e.synthetic = o.synthetic; e.runEvenIfClaimed = o.runEvenIfClaimed;
        e.failClosed = o.failClosed; e.fn = std::move(fn); e.ofn = std::move(ofn);
        e.state = std::make_shared<HandlerState>();           // re-registering also re-enables a tripped handler
        std::lock_guard<std::mutex> g(m_mtx);
        for (Entry& x : m_entries)
            if (x.stage == st && x.modId == modId && x.name == e.name) { x = std::move(e); RebuildLocked(); return true; }
        m_entries.push_back(std::move(e));
        RebuildLocked();
        return true;
    }

    void RebuildLocked() {
        auto s = std::make_shared<Snapshot>();
        s->overrideVersion = PriorityOverrides::Instance().Version();
        for (Entry e : m_entries) {                            // copy: snapshot owns its entries
            e.effPriority = e.basePriority + PriorityOverrides::Instance().Get(e.modId);
            (e.stage == Stage::Gate ? s->gates : e.stage == Stage::Action ? s->actions : s->observes).push_back(std::move(e));
        }
        auto cmp = [](const Entry& a, const Entry& b) {
            if (a.effPriority != b.effPriority) return a.effPriority > b.effPriority;
            if (a.modKey != b.modKey) return a.modKey < b.modKey;
            return a.name < b.name;
        };
        std::sort(s->gates.begin(), s->gates.end(), cmp);
        std::sort(s->actions.begin(), s->actions.end(), cmp);
        std::sort(s->observes.begin(), s->observes.end(), cmp);
        m_snap.store(std::move(s));
    }

    std::string m_name;
    uint64_t m_id;
    mutable std::mutex m_mtx;
    std::vector<Entry> m_entries;
    detail::AtomicSp<Snapshot> m_snap;
    mutable std::atomic<int32_t> m_inflight{0};
};

// ---------------------------------------------------------------------------------------------
// Usage sketch
//
//   struct InteractPayload { void* player; void* target; bool* allow; };
//   static EventChain<InteractPayload> g_interact("interaction_attempt", Hash("interaction_attempt"));
//
//   // hook (core):
//   bool Detour(void* player, void* target) {
//       InteractPayload p{player, target, nullptr};
//       DispatchResult r = g_interact.Dispatch(p);
//       if (r.canceled) return false;                         // skip the original
//       return Original(player, target);
//   }
//   // mods:
//   g_interact.RegisterGate("DreamCropGuard", [](EventContext<InteractPayload>& c) {
//       if (IsProtectedCrop(c.data.target)) c.Cancel("protected crop");
//   }, {.priority = 100, .failClosed = true});
//   g_interact.RegisterAction("DreamAoEHarvest", [](EventContext<InteractPayload>& c) {
//       if (!c.Claim()) return;                               // someone else already handled it
//       SyntheticScope s("DreamAoEHarvest");                  // neighbours re-enter the chain as synthetic
//       HarvestNeighbours(c.data.target);
//   }, {.priority = 0});
//   g_interact.RegisterObserve("DreamXpShare", [](const ObserveContext<InteractPayload>& c) {
//       if (!c.canceled && c.claimed) CountInteraction(c.claimedBy);
//   });
// ---------------------------------------------------------------------------------------------

} // namespace dreamkurumi::event
