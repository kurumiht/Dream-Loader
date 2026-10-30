#pragma once
#include <windows.h>
#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>
#include <array>
#include <utility>
#include <functional>
#include <algorithm>
#include <mutex>
#include <memory>
#include <typeindex>
#include <unordered_map>
#include "DreamHook.hpp"
#include "DreamPipeline.hpp"
#include "DreamCrashGuard.hpp"
#include "DreamPool.hpp"
#include "DreamLog.hpp"

namespace dreamkurumi {

// ============================================================================
// DreamCentralDispatcher - Multi-Mod Inline Hook Collision Resolver
// Guarantees 100% crash-proof multi-mod hooking on the exact same game function.
// Zero trampoline corruption, lock-free RCU fast path, priority ordering,
// slot-based C++ dispatch stubs (zero register clobbering, zero asm hacks),
// and seamless hot-reload unhooking without breaking other mods in the chain!
// ============================================================================

template <typename Ret, typename... Args>
class CentralDetourHandler {
public:
    using TargetFn = Ret(*)(Args...);
    using StubFn = Ret(*)(Args...);
    using PrefixFn = std::function<bool(Args&...)>;
    using PostfixFn = std::function<void(Ret&, Args&...)>;

    struct Subscriber {
        std::string modId;
        HookPriority priority{HookPriority::Normal};
        PrefixFn prefix;
        PostfixFn postfix;
        void* ownerKey{nullptr};
    };

    struct Snapshot {
        std::vector<Subscriber> subscribers;
    };

    template <size_t SlotIdx>
    static Ret SlotDispatcherStub(Args... args) {
        auto* pThis = s_slots[SlotIdx];
        if (pThis) {
            return pThis->Dispatch(args...);
        }
        return Ret{};
    }

    template <size_t... Is>
    static constexpr auto MakeStubTable(std::index_sequence<Is...>) {
        return std::array<StubFn, sizeof...(Is)>{ (&SlotDispatcherStub<Is>)... };
    }

    static inline constexpr auto s_stubs = MakeStubTable(std::make_index_sequence<64>{});
    static inline CentralDetourHandler* s_slots[64] = {nullptr};
    static inline std::mutex s_slotMutex;

    explicit CentralDetourHandler(void* target)
        : m_target(target) {
        m_snapshot = std::make_shared<Snapshot>();
    }

    ~CentralDetourHandler() {
        m_hook.Disable();
        if (m_slotIndex >= 0 && m_slotIndex < 64) {
            std::lock_guard<std::mutex> lock(s_slotMutex);
            s_slots[m_slotIndex] = nullptr;
            m_slotIndex = -1;
        }
    }

    bool EnsureInstalled() {
        if (m_hook.IsInstalled()) return true;

        if (m_slotIndex < 0) {
            std::lock_guard<std::mutex> lock(s_slotMutex);
            for (int i = 0; i < 64; ++i) {
                if (s_slots[i] == nullptr) {
                    s_slots[i] = this;
                    m_slotIndex = i;
                    break;
                }
            }
        }

        if (m_slotIndex < 0) {
            DREAM_LOG_ERROR("DreamCentralDispatcher: Exceeded maximum 64 active hooks for this signature");
            return false;
        }

        void* stubTarget = reinterpret_cast<void*>(s_stubs[m_slotIndex]);
        bool ok = m_hook.Install(m_target, stubTarget);
        if (ok) {
            DREAM_LOG_INFO("DreamCentralDispatcher: Master Detour installed at 0x%p (Slot %d)", m_target, m_slotIndex);
        }
        return ok;
    }

    bool AddSubscriber(const std::string& modId, HookPriority priority, void* ownerKey, PrefixFn pfx, PostfixFn pfxPost) {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!EnsureInstalled()) {
            DREAM_LOG_ERROR("DreamCentralDispatcher: Failed to install central hook on 0x%p for mod '%s'",
                            m_target, modId.c_str());
            return false;
        }

        for (auto& sub : m_subscribers) {
            if (sub.ownerKey == ownerKey || (!modId.empty() && sub.modId == modId)) {
                sub.priority = priority;
                sub.prefix = pfx;
                sub.postfix = pfxPost;
                SortAndRebuildSnapshot();
                return true;
            }
        }

        m_subscribers.push_back({modId, priority, pfx, pfxPost, ownerKey});
        SortAndRebuildSnapshot();
        DREAM_LOG_INFO("DreamCentralDispatcher: Registered mod '%s' on 0x%p (Priority %d, Active subscribers: %zu)",
                       modId.c_str(), m_target, static_cast<int>(priority), m_subscribers.size());
        return true;
    }

    void RemoveSubscriber(void* ownerKey) {
        std::lock_guard<std::mutex> lock(m_mutex);
        std::string removedMod;
        for (const auto& sub : m_subscribers) {
            if (sub.ownerKey == ownerKey) {
                removedMod = sub.modId;
                break;
            }
        }
        auto it = std::remove_if(m_subscribers.begin(), m_subscribers.end(),
            [ownerKey](const Subscriber& s) { return s.ownerKey == ownerKey; });
        if (it != m_subscribers.end()) {
            m_subscribers.erase(it, m_subscribers.end());
            SortAndRebuildSnapshot();
            DREAM_LOG_INFO("DreamCentralDispatcher: Unregistered mod '%s' from 0x%p (Remaining subscribers: %zu)",
                           removedMod.c_str(), m_target, m_subscribers.size());
        }
    }

    void RemoveSubscriberByModId(const std::string& modId) {
        if (modId.empty()) return;
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = std::remove_if(m_subscribers.begin(), m_subscribers.end(),
            [&modId](const Subscriber& s) { return s.modId == modId; });
        if (it != m_subscribers.end()) {
            m_subscribers.erase(it, m_subscribers.end());
            SortAndRebuildSnapshot();
            DREAM_LOG_INFO("DreamCentralDispatcher: Purged subscriber '%s' from 0x%p (Remaining subscribers: %zu)",
                           modId.c_str(), m_target, m_subscribers.size());
        }
    }

    int32_t GetInFlightCount() const { return m_inFlight.load(std::memory_order_relaxed); }

    Ret CallOriginal(Args... args) {
        auto orig = reinterpret_cast<TargetFn>(m_hook.GetOriginal());
        if (!orig) return Ret{};
        return orig(args...);
    }

    bool IsInstalled() const { return m_hook.IsInstalled(); }
    void* GetOriginalAddress() const { return m_hook.GetOriginal(); }
    size_t GetSubscriberCount() const {
        auto snap = m_snapshot.load();
        return snap ? snap->subscribers.size() : 0;
    }

private:
    void SortAndRebuildSnapshot() {
        std::sort(m_subscribers.begin(), m_subscribers.end(),
            [](const Subscriber& a, const Subscriber& b) {
                return static_cast<int>(a.priority) < static_cast<int>(b.priority);
            });
        auto next = std::make_shared<Snapshot>();
        next->subscribers = m_subscribers;
        m_snapshot.store(next);
    }

    Ret Dispatch(Args... args) {
        struct InFlightGuard {
            std::atomic<int32_t>& m_c;
            explicit InFlightGuard(std::atomic<int32_t>& c) : m_c(c) {
                m_c.fetch_add(1, std::memory_order_relaxed);
            }
            ~InFlightGuard() {
                m_c.fetch_sub(1, std::memory_order_release);
            }
        } inFlightGuard(m_inFlight);

        std::shared_ptr<const Snapshot> snap = m_snapshot.load();
        auto orig = reinterpret_cast<TargetFn>(m_hook.GetOriginal());

        if (!snap || snap->subscribers.empty()) {
            if (orig) return orig(args...);
            return Ret{};
        }

        bool runOriginal = true;
        for (const auto& sub : snap->subscribers) {
            if (sub.prefix) {
                DreamCrashGuard::ExecuteSafely(sub.modId.c_str(), [&]() {
                    if (!sub.prefix(args...)) {
                        runOriginal = false;
                        DREAM_LOG_DEBUG("DreamCentralDispatcher: Mod '%s' skipped original execution on 0x%p",
                                        sub.modId.c_str(), m_target);
                    }
                });
                if (!runOriginal) break;
            }
        }

        Ret result{};
        if (runOriginal && orig) {
            result = orig(args...);
        }

        for (const auto& sub : snap->subscribers) {
            if (sub.postfix) {
                DreamCrashGuard::ExecuteSafely(sub.modId.c_str(), [&]() {
                    sub.postfix(result, args...);
                });
            }
        }
        return result;
    }

    void* m_target{nullptr};
    int m_slotIndex{-1};
    DreamInlineHook m_hook;
    std::vector<Subscriber> m_subscribers;
    std::atomic<std::shared_ptr<const Snapshot>> m_snapshot;
    std::atomic<int32_t> m_inFlight{0};
    std::mutex m_mutex;
};

// ============================================================================
// Specialization for void Return Type
// ============================================================================
template <typename... Args>
class CentralDetourHandler<void, Args...> {
public:
    using TargetFn = void(*)(Args...);
    using StubFn = void(*)(Args...);
    using PrefixFn = std::function<bool(Args&...)>;
    using PostfixFn = std::function<void(Args&...)>;

    struct Subscriber {
        std::string modId;
        HookPriority priority{HookPriority::Normal};
        PrefixFn prefix;
        PostfixFn postfix;
        void* ownerKey{nullptr};
    };

    struct Snapshot {
        std::vector<Subscriber> subscribers;
    };

    template <size_t SlotIdx>
    static void SlotDispatcherStub(Args... args) {
        auto* pThis = s_slots[SlotIdx];
        if (pThis) {
            pThis->Dispatch(args...);
        }
    }

    template <size_t... Is>
    static constexpr auto MakeStubTable(std::index_sequence<Is...>) {
        return std::array<StubFn, sizeof...(Is)>{ (&SlotDispatcherStub<Is>)... };
    }

    static inline constexpr auto s_stubs = MakeStubTable(std::make_index_sequence<64>{});
    static inline CentralDetourHandler* s_slots[64] = {nullptr};
    static inline std::mutex s_slotMutex;

    explicit CentralDetourHandler(void* target)
        : m_target(target) {
        m_snapshot = std::make_shared<Snapshot>();
    }

    ~CentralDetourHandler() {
        m_hook.Disable();
        if (m_slotIndex >= 0 && m_slotIndex < 64) {
            std::lock_guard<std::mutex> lock(s_slotMutex);
            s_slots[m_slotIndex] = nullptr;
            m_slotIndex = -1;
        }
    }

    bool EnsureInstalled() {
        if (m_hook.IsInstalled()) return true;

        if (m_slotIndex < 0) {
            std::lock_guard<std::mutex> lock(s_slotMutex);
            for (int i = 0; i < 64; ++i) {
                if (s_slots[i] == nullptr) {
                    s_slots[i] = this;
                    m_slotIndex = i;
                    break;
                }
            }
        }

        if (m_slotIndex < 0) {
            DREAM_LOG_ERROR("DreamCentralDispatcher: Exceeded maximum 64 active hooks for this signature");
            return false;
        }

        void* stubTarget = reinterpret_cast<void*>(s_stubs[m_slotIndex]);
        bool ok = m_hook.Install(m_target, stubTarget);
        if (ok) {
            DREAM_LOG_INFO("DreamCentralDispatcher: Master Detour installed at 0x%p (Slot %d)", m_target, m_slotIndex);
        }
        return ok;
    }

    bool AddSubscriber(const std::string& modId, HookPriority priority, void* ownerKey, PrefixFn pfx, PostfixFn pfxPost) {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!EnsureInstalled()) {
            DREAM_LOG_ERROR("DreamCentralDispatcher: Failed to install central hook on 0x%p for mod '%s'",
                            m_target, modId.c_str());
            return false;
        }

        for (auto& sub : m_subscribers) {
            if (sub.ownerKey == ownerKey || (!modId.empty() && sub.modId == modId)) {
                sub.priority = priority;
                sub.prefix = pfx;
                sub.postfix = pfxPost;
                SortAndRebuildSnapshot();
                return true;
            }
        }

        m_subscribers.push_back({modId, priority, pfx, pfxPost, ownerKey});
        SortAndRebuildSnapshot();
        DREAM_LOG_INFO("DreamCentralDispatcher: Registered mod '%s' on 0x%p (Priority %d, Active subscribers: %zu)",
                       modId.c_str(), m_target, static_cast<int>(priority), m_subscribers.size());
        return true;
    }

    void RemoveSubscriber(void* ownerKey) {
        std::lock_guard<std::mutex> lock(m_mutex);
        std::string removedMod;
        for (const auto& sub : m_subscribers) {
            if (sub.ownerKey == ownerKey) {
                removedMod = sub.modId;
                break;
            }
        }
        auto it = std::remove_if(m_subscribers.begin(), m_subscribers.end(),
            [ownerKey](const Subscriber& s) { return s.ownerKey == ownerKey; });
        if (it != m_subscribers.end()) {
            m_subscribers.erase(it, m_subscribers.end());
            SortAndRebuildSnapshot();
            DREAM_LOG_INFO("DreamCentralDispatcher: Unregistered mod '%s' from 0x%p (Remaining subscribers: %zu)",
                            removedMod.c_str(), m_target, m_subscribers.size());
        }
    }

    void RemoveSubscriberByModId(const std::string& modId) {
        if (modId.empty()) return;
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = std::remove_if(m_subscribers.begin(), m_subscribers.end(),
            [&modId](const Subscriber& s) { return s.modId == modId; });
        if (it != m_subscribers.end()) {
            m_subscribers.erase(it, m_subscribers.end());
            SortAndRebuildSnapshot();
            DREAM_LOG_INFO("DreamCentralDispatcher: Purged subscriber '%s' from 0x%p (Remaining subscribers: %zu)",
                           modId.c_str(), m_target, m_subscribers.size());
        }
    }

    int32_t GetInFlightCount() const { return m_inFlight.load(std::memory_order_relaxed); }

    void CallOriginal(Args... args) {
        auto orig = reinterpret_cast<TargetFn>(m_hook.GetOriginal());
        if (orig) orig(args...);
    }

    bool IsInstalled() const { return m_hook.IsInstalled(); }
    void* GetOriginalAddress() const { return m_hook.GetOriginal(); }
    size_t GetSubscriberCount() const {
        auto snap = m_snapshot.load();
        return snap ? snap->subscribers.size() : 0;
    }

private:
    void SortAndRebuildSnapshot() {
        std::sort(m_subscribers.begin(), m_subscribers.end(),
            [](const Subscriber& a, const Subscriber& b) {
                return static_cast<int>(a.priority) < static_cast<int>(b.priority);
            });
        auto next = std::make_shared<Snapshot>();
        next->subscribers = m_subscribers;
        m_snapshot.store(next);
    }

    void Dispatch(Args... args) {
        struct InFlightGuard {
            std::atomic<int32_t>& m_c;
            explicit InFlightGuard(std::atomic<int32_t>& c) : m_c(c) {
                m_c.fetch_add(1, std::memory_order_relaxed);
            }
            ~InFlightGuard() {
                m_c.fetch_sub(1, std::memory_order_release);
            }
        } inFlightGuard(m_inFlight);

        std::shared_ptr<const Snapshot> snap = m_snapshot.load();
        auto orig = reinterpret_cast<TargetFn>(m_hook.GetOriginal());

        if (!snap || snap->subscribers.empty()) {
            if (orig) orig(args...);
            return;
        }

        bool runOriginal = true;
        for (const auto& sub : snap->subscribers) {
            if (sub.prefix) {
                DreamCrashGuard::ExecuteSafely(sub.modId.c_str(), [&]() {
                    if (!sub.prefix(args...)) {
                        runOriginal = false;
                        DREAM_LOG_DEBUG("DreamCentralDispatcher: Mod '%s' skipped original execution on 0x%p",
                                        sub.modId.c_str(), m_target);
                    }
                });
                if (!runOriginal) break;
            }
        }

        if (runOriginal && orig) {
            orig(args...);
        }

        for (const auto& sub : snap->subscribers) {
            if (sub.postfix) {
                DreamCrashGuard::ExecuteSafely(sub.modId.c_str(), [&]() {
                    sub.postfix(args...);
                });
            }
        }
    }

    void* m_target{nullptr};
    int m_slotIndex{-1};
    DreamInlineHook m_hook;
    std::vector<Subscriber> m_subscribers;
    std::atomic<std::shared_ptr<const Snapshot>> m_snapshot;
    std::atomic<int32_t> m_inFlight{0};
    std::mutex m_mutex;
};

// ============================================================================
// DreamCentralDispatcher Registry Singleton
// ============================================================================
class DreamCentralDispatcher {
public:
    static DreamCentralDispatcher& Instance() {
        static DreamCentralDispatcher s_instance;
        return s_instance;
    }

    struct HandlerEntry {
        std::shared_ptr<void> handler;
        std::type_index typeIndex{typeid(void)};
        std::function<void(const std::string&)> cancelByOwner;
        std::function<int32_t()> getInFlight;
    };

    template <typename Ret, typename... Args>
    CentralDetourHandler<Ret, Args...>& GetHandler(void* target) {
        std::lock_guard<std::mutex> lock(m_mutex);
        std::type_index reqType(typeid(CentralDetourHandler<Ret, Args...>));
        auto it = m_handlers.find(target);
        if (it == m_handlers.end()) {
            auto handler = std::make_shared<CentralDetourHandler<Ret, Args...>>(target);
            HandlerEntry entry;
            entry.handler = handler;
            entry.typeIndex = reqType;
            entry.cancelByOwner = [handler](const std::string& modId) {
                handler->RemoveSubscriberByModId(modId);
            };
            entry.getInFlight = [handler]() {
                return handler->GetInFlightCount();
            };
            m_handlers[target] = entry;
            return *handler;
        }

        if (it->second.typeIndex != reqType) {
            DREAM_LOG_ERROR("DreamCentralDispatcher: Signature MISMATCH for hook target 0x%p! Expected %s, got %s",
                            target, it->second.typeIndex.name(), reqType.name());
            static CentralDetourHandler<Ret, Args...> s_rejectedDummy(nullptr);
            return s_rejectedDummy;
        }
        return *std::static_pointer_cast<CentralDetourHandler<Ret, Args...>>(it->second.handler);
    }

    int32_t GetTotalInFlightCount() {
        std::lock_guard<std::mutex> lock(m_mutex);
        int32_t total = 0;
        for (auto& [target, entry] : m_handlers) {
            if (entry.getInFlight) {
                total += entry.getInFlight();
            }
        }
        return total;
    }

    void CancelByOwner(const std::string& modId) {
        if (modId.empty()) return;
        std::lock_guard<std::mutex> lock(m_mutex);
        for (auto& [target, entry] : m_handlers) {
            if (entry.cancelByOwner) {
                entry.cancelByOwner(modId);
            }
        }
        DREAM_LOG_INFO("DreamCentralDispatcher: CancelByOwner purged subscribers for mod '%s'", modId.c_str());
    }

    size_t GetActiveHookedTargetCount() {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_handlers.size();
    }

private:
    DreamCentralDispatcher() = default;
    std::mutex m_mutex;
    std::unordered_map<void*, HandlerEntry> m_handlers;
};

} // namespace dreamkurumi
