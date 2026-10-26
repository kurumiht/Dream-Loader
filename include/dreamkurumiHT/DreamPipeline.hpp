#pragma once
#include <windows.h>
#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>
#include <functional>
#include <algorithm>
#include <mutex>
#include <memory>
#include <unordered_map>
#include "DreamHook.hpp"
#include "DreamLog.hpp"

namespace dreamkurumi {

// ============================================================================
// Harmony Hook Priority
// Smaller numbers execute earlier (e.g. First -> Highest -> Normal -> Lowest -> Last)
// ============================================================================
enum class HookPriority : int {
    First   = 0,
    Highest = 100,
    High    = 200,
    Normal  = 300,
    Low     = 400,
    Lowest  = 500,
    Last    = 600
};

// ============================================================================
// Hook Pipeline Implementation
// ============================================================================
template <typename Ret, typename... Args>
class HookPipeline {
public:
    using PrefixFn = std::function<bool(Args&...)>;
    using PostfixFn = std::function<void(Ret&, Args&...)>;
    using OriginalFn = Ret(*)(Args...);

    struct PrefixEntry {
        std::string modId;
        HookPriority priority;
        PrefixFn callback;
    };

    struct PostfixEntry {
        std::string modId;
        HookPriority priority;
        PostfixFn callback;
    };

    struct PipelineSnapshot {
        std::vector<PrefixEntry> prefixes;
        std::vector<PostfixEntry> postfixes;
    };

    HookPipeline() {
        m_snapshot = std::make_shared<PipelineSnapshot>();
    }

    void AddPrefix(const std::string& modId, HookPriority priority, PrefixFn fn) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_prefixes.push_back({modId, priority, fn});
        SortPrefixes();
        UpdateSnapshot();
        DREAM_LOG_INFO("HookPipeline: Added Prefix from mod '%s' (Priority %d, Total prefixes: %zu)",
                       modId.c_str(), static_cast<int>(priority), m_prefixes.size());
    }

    void AddPostfix(const std::string& modId, HookPriority priority, PostfixFn fn) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_postfixes.push_back({modId, priority, fn});
        SortPostfixes();
        UpdateSnapshot();
        DREAM_LOG_INFO("HookPipeline: Added Postfix from mod '%s' (Priority %d, Total postfixes: %zu)",
                       modId.c_str(), static_cast<int>(priority), m_postfixes.size());
    }

    void RemoveMod(const std::string& modId) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_prefixes.erase(std::remove_if(m_prefixes.begin(), m_prefixes.end(),
            [&](const auto& e) { return e.modId == modId; }), m_prefixes.end());
        m_postfixes.erase(std::remove_if(m_postfixes.begin(), m_postfixes.end(),
            [&](const auto& e) { return e.modId == modId; }), m_postfixes.end());
        UpdateSnapshot();
        DREAM_LOG_INFO("HookPipeline: Removed all hooks for mod '%s'", modId.c_str());
    }

    Ret Dispatch(OriginalFn original, Args... args) {
        // Lock-Free Fast Path (RCU Snapshot): Zero Mutex Contention, Zero Heap Allocations!
        std::shared_ptr<const PipelineSnapshot> snapshot = m_snapshot.load();
        if (!snapshot) {
            return original ? original(args...) : Ret{};
        }

        bool runOriginal = true;
        for (const auto& entry : snapshot->prefixes) {
            if (!entry.callback(args...)) {
                runOriginal = false;
                DREAM_LOG_DEBUG("HookPipeline: Prefix '%s' skipped original execution!", entry.modId.c_str());
                break;
            }
        }

        Ret result{};
        if (runOriginal && original) {
            result = original(args...);
        }

        for (const auto& entry : snapshot->postfixes) {
            entry.callback(result, args...);
        }

        return result;
    }

    size_t GetPrefixCount() {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_prefixes.size();
    }

    size_t GetPostfixCount() {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_postfixes.size();
    }

private:
    void UpdateSnapshot() {
        auto next = std::make_shared<PipelineSnapshot>();
        next->prefixes = m_prefixes;
        next->postfixes = m_postfixes;
        m_snapshot.store(next);
    }

    void SortPrefixes() {
        std::sort(m_prefixes.begin(), m_prefixes.end(),
            [](const auto& a, const auto& b) {
                return static_cast<int>(a.priority) < static_cast<int>(b.priority);
            });
    }

    void SortPostfixes() {
        std::sort(m_postfixes.begin(), m_postfixes.end(),
            [](const auto& a, const auto& b) {
                return static_cast<int>(a.priority) < static_cast<int>(b.priority);
            });
    }

    std::mutex m_mutex;
    std::vector<PrefixEntry> m_prefixes;
    std::vector<PostfixEntry> m_postfixes;
    std::atomic<std::shared_ptr<const PipelineSnapshot>> m_snapshot;
};

// ============================================================================
// Specialization for void Return Type
// ============================================================================
template <typename... Args>
class HookPipeline<void, Args...> {
public:
    using PrefixFn = std::function<bool(Args&...)>;
    using PostfixFn = std::function<void(Args&...)>;
    using OriginalFn = void(*)(Args...);

    struct PrefixEntry {
        std::string modId;
        HookPriority priority;
        PrefixFn callback;
    };

    struct PostfixEntry {
        std::string modId;
        HookPriority priority;
        PostfixFn callback;
    };

    struct PipelineSnapshot {
        std::vector<PrefixEntry> prefixes;
        std::vector<PostfixEntry> postfixes;
    };

    HookPipeline() {
        m_snapshot = std::make_shared<PipelineSnapshot>();
    }

    void AddPrefix(const std::string& modId, HookPriority priority, PrefixFn fn) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_prefixes.push_back({modId, priority, fn});
        SortPrefixes();
        UpdateSnapshot();
    }

    void AddPostfix(const std::string& modId, HookPriority priority, PostfixFn fn) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_postfixes.push_back({modId, priority, fn});
        SortPostfixes();
        UpdateSnapshot();
    }

    void RemoveMod(const std::string& modId) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_prefixes.erase(std::remove_if(m_prefixes.begin(), m_prefixes.end(),
            [&](const auto& e) { return e.modId == modId; }), m_prefixes.end());
        m_postfixes.erase(std::remove_if(m_postfixes.begin(), m_postfixes.end(),
            [&](const auto& e) { return e.modId == modId; }), m_postfixes.end());
        UpdateSnapshot();
    }

    void Dispatch(OriginalFn original, Args... args) {
        // Lock-Free Fast Path (RCU Snapshot): Zero Mutex Contention, Zero Heap Allocations!
        std::shared_ptr<const PipelineSnapshot> snapshot = m_snapshot.load();
        if (!snapshot) {
            if (original) original(args...);
            return;
        }

        bool runOriginal = true;
        for (const auto& entry : snapshot->prefixes) {
            if (!entry.callback(args...)) {
                runOriginal = false;
                break;
            }
        }

        if (runOriginal && original) {
            original(args...);
        }

        for (const auto& entry : snapshot->postfixes) {
            entry.callback(args...);
        }
    }

    size_t GetPrefixCount() {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_prefixes.size();
    }

    size_t GetPostfixCount() {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_postfixes.size();
    }

private:
    void UpdateSnapshot() {
        auto next = std::make_shared<PipelineSnapshot>();
        next->prefixes = m_prefixes;
        next->postfixes = m_postfixes;
        m_snapshot.store(next);
    }

    void SortPrefixes() {
        std::sort(m_prefixes.begin(), m_prefixes.end(),
            [](const auto& a, const auto& b) {
                return static_cast<int>(a.priority) < static_cast<int>(b.priority);
            });
    }

    void SortPostfixes() {
        std::sort(m_postfixes.begin(), m_postfixes.end(),
            [](const auto& a, const auto& b) {
                return static_cast<int>(a.priority) < static_cast<int>(b.priority);
            });
    }

    std::mutex m_mutex;
    std::vector<PrefixEntry> m_prefixes;
    std::vector<PostfixEntry> m_postfixes;
    std::atomic<std::shared_ptr<const PipelineSnapshot>> m_snapshot;
};

// ============================================================================
// DreamChainedHook - Manages multi-mod hooking on a single target address
// ============================================================================
template <typename FnSig>
class DreamChainedHook;

template <typename Ret, typename... Args>
class DreamChainedHook<Ret(Args...)> {
public:
    using TargetFn = Ret(*)(Args...);

    explicit DreamChainedHook(void* target)
        : m_target(target) {}

    ~DreamChainedHook() {
        m_hook.Disable();
    }

    static DreamChainedHook& Get(void* target) {
        static std::mutex s_registryMutex;
        static std::unordered_map<void*, std::unique_ptr<DreamChainedHook>> s_registry;

        std::lock_guard<std::mutex> lock(s_registryMutex);
        auto& instance = s_registry[target];
        if (!instance) {
            instance = std::make_unique<DreamChainedHook>(target);
        }
        return *instance;
    }

    void AddPrefix(const std::string& modId, HookPriority priority, typename HookPipeline<Ret, Args...>::PrefixFn fn) {
        EnsureInstalled();
        m_pipeline.AddPrefix(modId, priority, fn);
    }

    void AddPostfix(const std::string& modId, HookPriority priority, typename HookPipeline<Ret, Args...>::PostfixFn fn) {
        EnsureInstalled();
        m_pipeline.AddPostfix(modId, priority, fn);
    }

    void RemoveMod(const std::string& modId) {
        m_pipeline.RemoveMod(modId);
    }

    bool IsInstalled() const {
        return m_hook.IsInstalled();
    }

private:
    bool EnsureInstalled() {
        if (m_hook.IsInstalled()) return true;

        s_activeInstance = this;
        return m_hook.Install(m_target, reinterpret_cast<void*>(StaticDispatcher));
    }

    static Ret StaticDispatcher(Args... args) {
        if (!s_activeInstance) return Ret{};
        auto original = reinterpret_cast<TargetFn>(s_activeInstance->m_hook.GetOriginal());
        return s_activeInstance->m_pipeline.Dispatch(original, args...);
    }

    inline static DreamChainedHook* s_activeInstance{nullptr};

    void* m_target{nullptr};
    DreamInlineHook m_hook;
    HookPipeline<Ret, Args...> m_pipeline;
};

} // namespace dreamkurumi
