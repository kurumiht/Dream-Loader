#pragma once
#include <type_traits>
#include <vector>
#include <string>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <algorithm>
#include <functional>
#include "DreamHook.hpp"
#include "DreamPipeline.hpp"
#include "DreamFuzzyScanner.hpp"
#include "DreamCache.hpp"
#include "DreamCrashGuard.hpp"
#include "DreamLog.hpp"

namespace dreamkurumi {

// ============================================================================
// Patch Metadata Traits Template
// ============================================================================
template <typename T>
struct PatchMetadata {
    static constexpr const char* TargetSymbol = nullptr;
    static constexpr uintptr_t TargetFallbackRva = 0;
    static constexpr const char* TargetPattern = nullptr;
    static constexpr const char* TargetSecondaryPattern = nullptr;
    static constexpr HookPriority Priority = HookPriority::Normal;
};

// ============================================================================
// Patch Registration Interface
// ============================================================================
class IPatchRegistration {
public:
    virtual ~IPatchRegistration() = default;
    virtual const char* GetName() const = 0;
    virtual bool InstallPatch() = 0;
    virtual bool DisablePatch() = 0;
    virtual bool IsInstalled() const = 0;
    virtual void* GetTargetAddress() const = 0;
    virtual const char* GetResolutionMethod() const = 0;
};

// ============================================================================
// Central Harmony Registry (Manages PatchAll, UnpatchAll & Health Table)
// ============================================================================
class DreamHarmonyRegistry {
public:
    static DreamHarmonyRegistry& Instance() {
        static DreamHarmonyRegistry s_instance;
        return s_instance;
    }

    void Register(IPatchRegistration* patch) {
        m_patches.push_back(patch);
    }

    size_t PatchAll() {
        size_t count = 0;
        DREAM_LOG_INFO("DreamHarmony: Executing PatchAll (%zu registered patches)...", m_patches.size());
        for (auto* p : m_patches) {
            if (p->InstallPatch()) {
                DREAM_LOG_INFO("  [OK] Installed patch: %s (%s at %p)", p->GetName(), p->GetResolutionMethod(), p->GetTargetAddress());
                count++;
            } else {
                DREAM_LOG_WARN("  [FAIL] Could not install patch: %s (%s)", p->GetName(), p->GetResolutionMethod());
            }
        }
        DREAM_LOG_INFO("DreamHarmony: PatchAll complete (%zu/%zu succeeded)", count, m_patches.size());

        PrintHookHealthTable();
        return count;
    }

    void PrintHookHealthTable() {
        DREAM_LOG_INFO("\n+---------------------------------------------------------------------------------------------------------+");
        DREAM_LOG_INFO("|                                 DreamHarmony Hook Health Diagnostics Table                              |");
        DREAM_LOG_INFO("+----------------------------+--------------------+-------------------------+-----------------------------+");
        DREAM_LOG_INFO("| Patch / Hook Name          | Target Address     | Resolution Method       | Health Status               |");
        DREAM_LOG_INFO("+----------------------------+--------------------+-------------------------+-----------------------------+");
        for (auto* p : m_patches) {
            char addrStr[32];
            if (p->GetTargetAddress()) {
                snprintf(addrStr, sizeof(addrStr), "0x%016llX", (unsigned long long)(uintptr_t)p->GetTargetAddress());
            } else {
                snprintf(addrStr, sizeof(addrStr), "(Unresolved)");
            }
            const char* statusStr = p->IsInstalled() ? "[HEALTHY / ACTIVE]" : "[FAILED]";
            DREAM_LOG_INFO("| %-26s | %-18s | %-23s | %-27s |",
                           p->GetName(), addrStr, p->GetResolutionMethod(), statusStr);
        }
        DREAM_LOG_INFO("+----------------------------+--------------------+-------------------------+-----------------------------+\n");
    }

    void UnpatchAll() {
        DREAM_LOG_INFO("DreamHarmony: Executing UnpatchAll...");
        for (auto it = m_patches.rbegin(); it != m_patches.rend(); ++it) {
            if ((*it)->IsInstalled()) {
                (*it)->DisablePatch();
                DREAM_LOG_INFO("  [UNPATCH] Disabled patch: %s", (*it)->GetName());
            }
        }
    }

    const std::vector<IPatchRegistration*>& GetPatches() const {
        return m_patches;
    }

private:
    std::vector<IPatchRegistration*> m_patches;
};

// Top-level DreamHarmony static facade
class DreamHarmony {
public:
    static size_t PatchAll() {
        return DreamHarmonyRegistry::Instance().PatchAll();
    }

    static void UnpatchAll() {
        DreamHarmonyRegistry::Instance().UnpatchAll();
    }
};

// ============================================================================
// Multi-Mod Central Detour Pipeline per Target Address (Signature-Independent)
// ============================================================================
template <typename Ret, typename... Args>
class CentralHookPipeline {
public:
    using TargetFn = Ret(*)(Args...);
    using PrefixFn = std::function<bool(Args&...)>;
    using PostfixFn = std::function<void(Ret&, Args&...)>;

    struct Subscriber {
        std::string name;
        HookPriority priority{HookPriority::Normal};
        void* ownerKey{nullptr};
        PrefixFn prefix;
        PostfixFn postfix;
    };

    bool EnsureHookInstalled(void* target, void* dispatcher) {
        if (m_hook.IsInstalled()) return true;
        return m_hook.Install(target, dispatcher);
    }

    void DisableHook() {
        m_hook.Disable();
    }

    bool IsHookInstalled() const {
        return m_hook.IsInstalled();
    }

    TargetFn GetOriginal() const {
        return reinterpret_cast<TargetFn>(m_hook.GetOriginal());
    }

    void AddSubscriber(const std::string& name, HookPriority priority, void* ownerKey, PrefixFn pfx, PostfixFn post) {
        std::lock_guard<std::mutex> lock(m_mutex);
        for (auto& s : m_subscribers) {
            if (s.ownerKey == ownerKey) {
                s.priority = priority;
                s.prefix = pfx;
                s.postfix = post;
                SortSubscribers();
                m_snapshot.store(std::make_shared<const std::vector<Subscriber>>(m_subscribers));
                return;
            }
        }
        m_subscribers.push_back({name, priority, ownerKey, pfx, post});
        SortSubscribers();
        m_snapshot.store(std::make_shared<const std::vector<Subscriber>>(m_subscribers));
    }

    void RemoveSubscriber(void* ownerKey) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_subscribers.erase(std::remove_if(m_subscribers.begin(), m_subscribers.end(),
            [ownerKey](const Subscriber& s) { return s.ownerKey == ownerKey; }), m_subscribers.end());
        m_snapshot.store(std::make_shared<const std::vector<Subscriber>>(m_subscribers));
    }

    size_t GetSubscriberCount() {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_subscribers.size();
    }

    Ret Dispatch(Args... args) {
        auto snap = m_snapshot.load();
        auto orig = GetOriginal();
        if (!snap || snap->empty()) {
            return orig ? orig(args...) : Ret{};
        }

        bool runOriginal = true;
        for (const auto& s : *snap) {
            if (s.prefix) {
                DreamCrashGuard::ExecuteSafely(s.name.c_str(), [&]() {
                    if (!s.prefix(args...)) {
                        runOriginal = false;
                    }
                });
                if (!runOriginal) break;
            }
        }

        Ret result{};
        if (runOriginal && orig) {
            result = orig(args...);
        }

        for (const auto& s : *snap) {
            if (s.postfix) {
                DreamCrashGuard::ExecuteSafely(s.name.c_str(), [&]() {
                    s.postfix(result, args...);
                });
            }
        }
        return result;
    }

private:
    void SortSubscribers() {
        std::sort(m_subscribers.begin(), m_subscribers.end(),
            [](const Subscriber& a, const Subscriber& b) {
                return static_cast<int>(a.priority) < static_cast<int>(b.priority);
            });
    }

    DreamInlineHook m_hook;
    std::mutex m_mutex;
    std::vector<Subscriber> m_subscribers;
    std::atomic<std::shared_ptr<const std::vector<Subscriber>>> m_snapshot;
};

// Void specialization
template <typename... Args>
class CentralHookPipeline<void, Args...> {
public:
    using TargetFn = void(*)(Args...);
    using PrefixFn = std::function<bool(Args&...)>;
    using PostfixFn = std::function<void(Args&...)>;

    struct Subscriber {
        std::string name;
        HookPriority priority{HookPriority::Normal};
        void* ownerKey{nullptr};
        PrefixFn prefix;
        PostfixFn postfix;
    };

    bool EnsureHookInstalled(void* target, void* dispatcher) {
        if (m_hook.IsInstalled()) return true;
        return m_hook.Install(target, dispatcher);
    }

    void DisableHook() {
        m_hook.Disable();
    }

    bool IsHookInstalled() const {
        return m_hook.IsInstalled();
    }

    TargetFn GetOriginal() const {
        return reinterpret_cast<TargetFn>(m_hook.GetOriginal());
    }

    void AddSubscriber(const std::string& name, HookPriority priority, void* ownerKey, PrefixFn pfx, PostfixFn post) {
        std::lock_guard<std::mutex> lock(m_mutex);
        for (auto& s : m_subscribers) {
            if (s.ownerKey == ownerKey) {
                s.priority = priority;
                s.prefix = pfx;
                s.postfix = post;
                SortSubscribers();
                m_snapshot.store(std::make_shared<const std::vector<Subscriber>>(m_subscribers));
                return;
            }
        }
        m_subscribers.push_back({name, priority, ownerKey, pfx, post});
        SortSubscribers();
        m_snapshot.store(std::make_shared<const std::vector<Subscriber>>(m_subscribers));
    }

    void RemoveSubscriber(void* ownerKey) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_subscribers.erase(std::remove_if(m_subscribers.begin(), m_subscribers.end(),
            [ownerKey](const Subscriber& s) { return s.ownerKey == ownerKey; }), m_subscribers.end());
        m_snapshot.store(std::make_shared<const std::vector<Subscriber>>(m_subscribers));
    }

    size_t GetSubscriberCount() {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_subscribers.size();
    }

    void Dispatch(Args... args) {
        auto snap = m_snapshot.load();
        auto orig = GetOriginal();
        if (!snap || snap->empty()) {
            if (orig) orig(args...);
            return;
        }

        bool runOriginal = true;
        for (const auto& s : *snap) {
            if (s.prefix) {
                DreamCrashGuard::ExecuteSafely(s.name.c_str(), [&]() {
                    if (!s.prefix(args...)) {
                        runOriginal = false;
                    }
                });
                if (!runOriginal) break;
            }
        }

        if (runOriginal && orig) {
            orig(args...);
        }

        for (const auto& s : *snap) {
            if (s.postfix) {
                DreamCrashGuard::ExecuteSafely(s.name.c_str(), [&]() {
                    s.postfix(args...);
                });
            }
        }
    }

private:
    void SortSubscribers() {
        std::sort(m_subscribers.begin(), m_subscribers.end(),
            [](const Subscriber& a, const Subscriber& b) {
                return static_cast<int>(a.priority) < static_cast<int>(b.priority);
            });
    }

    DreamInlineHook m_hook;
    std::mutex m_mutex;
    std::vector<Subscriber> m_subscribers;
    std::atomic<std::shared_ptr<const std::vector<Subscriber>>> m_snapshot;
};

// Global pipeline registry shared across ALL Derived patches with signature Ret(Args...)
template <typename Ret, typename... Args>
class CentralHookPipelineRegistry {
public:
    static CentralHookPipeline<Ret, Args...>& Get(void* target) {
        static std::mutex s_mutex;
        static std::unordered_map<void*, std::unique_ptr<CentralHookPipeline<Ret, Args...>>> s_pipes;
        std::lock_guard<std::mutex> lock(s_mutex);
        auto& p = s_pipes[target];
        if (!p) p = std::make_unique<CentralHookPipeline<Ret, Args...>>();
        return *p;
    }
};

// ============================================================================
// Harmony Patch Base Class Template
// ============================================================================
template <typename Derived, typename Ret, typename... Args>
class DreamPatchBase {
public:
    using ReturnType = Ret;
    using TargetFn = Ret(*)(Args...);

    // Call the original unhooked function
    static Ret CallOriginal(Args... args) {
        void* target = s_targetAddress;
        if (target) {
            auto orig = CentralHookPipelineRegistry<Ret, Args...>::Get(target).GetOriginal();
            if constexpr (std::is_void_v<Ret>) {
                if (orig) orig(args...);
                return;
            } else {
                return orig ? orig(args...) : Ret{};
            }
        }
        return Ret{};
    }

    static void* GetOriginal() {
        if (s_targetAddress) {
            return reinterpret_cast<void*>(CentralHookPipelineRegistry<Ret, Args...>::Get(s_targetAddress).GetOriginal());
        }
        return nullptr;
    }

    // Central dispatcher invoked by CPU when hooked function is called
    static Ret HookDispatcher(Args... args) {
        void* target = s_targetAddress;
        if (target) {
            auto& pipe = CentralHookPipelineRegistry<Ret, Args...>::Get(target);
            if constexpr (std::is_void_v<Ret>) {
                pipe.Dispatch(args...);
                return;
            } else {
                return pipe.Dispatch(args...);
            }
        }

        if constexpr (!std::is_void_v<Ret>) return Ret{};
    }

    // Install on target address (multi-mod collision proof!)
    static bool Install(void* targetAddress) {
        if (!targetAddress) return false;
        s_targetAddress = targetAddress;

        auto& pipe = CentralHookPipelineRegistry<Ret, Args...>::Get(targetAddress);
        HookPriority priority = PatchMetadata<Derived>::Priority;
        const char* name = PatchMetadata<Derived>::TargetSymbol ? PatchMetadata<Derived>::TargetSymbol : "HarmonyPatch";

        typename CentralHookPipeline<Ret, Args...>::PrefixFn pfx = [](Args&... args) -> bool {
            bool runOriginal = true;
            if constexpr (requires { { Derived::Prefix(args...) } -> std::same_as<bool>; }) {
                runOriginal = Derived::Prefix(args...);
            } else if constexpr (requires { Derived::Prefix(args...); }) {
                Derived::Prefix(args...);
                runOriginal = true;
            }
            return runOriginal;
        };

        if constexpr (std::is_void_v<Ret>) {
            typename CentralHookPipeline<void, Args...>::PostfixFn post = [](Args&... args) {
                if constexpr (requires { Derived::Postfix(args...); }) {
                    Derived::Postfix(args...);
                }
            };
            pipe.AddSubscriber(name, priority, (void*)&s_autoRegister, pfx, post);
        } else {
            typename CentralHookPipeline<Ret, Args...>::PostfixFn post = [](Ret& result, Args&... args) {
                if constexpr (requires { Derived::Postfix(result, args...); }) {
                    Derived::Postfix(result, args...);
                } else if constexpr (requires { Derived::Postfix(args...); }) {
                    Derived::Postfix(args...);
                }
            };
            pipe.AddSubscriber(name, priority, (void*)&s_autoRegister, pfx, post);
        }

        // Install underlying master hook once
        if (!pipe.IsHookInstalled()) {
            bool ok = pipe.EnsureHookInstalled(targetAddress, reinterpret_cast<void*>(HookDispatcher));
            if (!ok) return false;
        }

        s_isInstalled = true;
        return true;
    }

    static bool InstallSymbol(const std::string& symbolName, uintptr_t fallbackRva = 0) {
        void* addr = DreamCache::Instance().Resolve(symbolName, fallbackRva);
        if (!addr) return false;
        return Install(addr);
    }

    static bool Disable() {
        if (s_targetAddress) {
            auto& pipe = CentralHookPipelineRegistry<Ret, Args...>::Get(s_targetAddress);
            pipe.RemoveSubscriber((void*)&s_autoRegister);
            s_isInstalled = false;
            // Only disable physical hook if no subscribers remain
            if (pipe.GetSubscriberCount() == 0) {
                pipe.DisableHook();
            }
            return true;
        }
        return false;
    }

    static bool Enable() {
        if (s_targetAddress) {
            return Install(s_targetAddress);
        }
        return false;
    }

    static bool IsInstalled() {
        return s_isInstalled;
    }

    // Auto-registration mechanism for PatchAll
    class AutoRegister : public IPatchRegistration {
    public:
        void* m_resolvedAddress{nullptr};
        const char* m_resolutionMethod{"Unresolved"};

        AutoRegister() {
            DreamHarmonyRegistry::Instance().Register(this);
        }

        const char* GetName() const override {
            if constexpr (PatchMetadata<Derived>::TargetSymbol != nullptr) {
                return PatchMetadata<Derived>::TargetSymbol;
            }
            return "UnnamedPatch";
        }

        void* GetTargetAddress() const override {
            return m_resolvedAddress;
        }

        const char* GetResolutionMethod() const override {
            return m_resolutionMethod;
        }

        bool InstallPatch() override {
            if constexpr (PatchMetadata<Derived>::TargetSymbol != nullptr) {
                uintptr_t fallback = PatchMetadata<Derived>::TargetFallbackRva;
                const char* primaryPattern = PatchMetadata<Derived>::TargetPattern;
                const char* secondaryPattern = PatchMetadata<Derived>::TargetSecondaryPattern;

                void* addr = nullptr;

                // 1. Try Primary AOB Pattern
                if (primaryPattern) {
                    addr = DreamCache::Instance().ResolvePattern(PatchMetadata<Derived>::TargetSymbol, primaryPattern, 0);
                    if (addr) {
                        m_resolutionMethod = "Primary AOB";
                    }
                }

                // 2. Try Secondary Fallback AOB Pattern
                if (!addr && secondaryPattern) {
                    std::string secSymbol = std::string(PatchMetadata<Derived>::TargetSymbol) + "_Sec";
                    addr = DreamCache::Instance().ResolvePattern(secSymbol, secondaryPattern, 0);
                    if (addr) {
                        m_resolutionMethod = "Secondary AOB (Fallback)";
                        DREAM_LOG_WARN("DreamHarmony: Patch '%s' resolved via Secondary Fallback AOB pattern!", PatchMetadata<Derived>::TargetSymbol);
                    }
                }

                // 3. Try Automatic Fuzzy Instruction-Aware Scan
                if (!addr && primaryPattern) {
                    void* fuzzyAddr = DreamFuzzyScanner::FindFuzzyPattern(primaryPattern);
                    if (fuzzyAddr) {
                        addr = fuzzyAddr;
                        m_resolutionMethod = "Fuzzy Instruction Scan";
                        uintptr_t modBase = DreamCache::Instance().GetModuleBase();
                        if (modBase) {
                            DreamCache::Instance().Store(PatchMetadata<Derived>::TargetSymbol, (uintptr_t)addr - modBase);
                        }
                        DREAM_LOG_WARN("DreamHarmony: Patch '%s' resolved via [Fuzzy Instruction Scan]!", PatchMetadata<Derived>::TargetSymbol);
                    }
                }

                // 4. Try Symbol Cache or Fallback Direct RVA
                if (!addr) {
                    addr = DreamCache::Instance().Resolve(PatchMetadata<Derived>::TargetSymbol, fallback);
                    if (addr) {
                        m_resolutionMethod = (primaryPattern || secondaryPattern) ? "Fallback Direct RVA" : "Symbol Cache / RVA";
                    }
                }

                if (addr) {
                    m_resolvedAddress = addr;
                    return Install(addr);
                } else {
                    m_resolutionMethod = "Resolution FAILED";
                }
            }
            return false;
        }

        bool DisablePatch() override {
            return Disable();
        }

        bool IsInstalled() const override {
            return DreamPatchBase<Derived, Ret, Args...>::IsInstalled();
        }
    };

    inline static AutoRegister s_autoRegister{};
    inline static const void* _forceInit = &s_autoRegister;
    inline static void* s_targetAddress{nullptr};
    inline static bool s_isInstalled{false};
};

// Convenient template alias
template <typename Derived, typename FnSig>
class DreamPatch;

template <typename Derived, typename Ret, typename... Args>
class DreamPatch<Derived, Ret(Args...)> : public DreamPatchBase<Derived, Ret, Args...> {};

} // namespace dreamkurumi

// Macro for basic manual patch declarations
#define DREAM_DECLARE_PATCH(PatchClass, RetType, ...)     struct PatchClass : public ::dreamkurumi::DreamPatch<PatchClass, RetType(__VA_ARGS__)>

// Macro for full Harmony-style auto-registering patches (supports PatchAll!)
#define DREAM_HARMONY_PATCH(PatchClass, SymbolNameStr, FallbackRvaVal, RetType, ...)     struct PatchClass;     namespace dreamkurumi {         template <> struct PatchMetadata<PatchClass> {             static constexpr const char* TargetSymbol = SymbolNameStr;             static constexpr uintptr_t TargetFallbackRva = FallbackRvaVal;             static constexpr const char* TargetPattern = nullptr;             static constexpr const char* TargetSecondaryPattern = nullptr;             static constexpr HookPriority Priority = HookPriority::Normal;         };     }     struct PatchClass : public ::dreamkurumi::DreamPatch<PatchClass, RetType(__VA_ARGS__)>

// Macro for Harmony-style patch with custom HookPriority
#define DREAM_HARMONY_PATCH_PRIORITY(PatchClass, SymbolNameStr, PriorityVal, FallbackRvaVal, RetType, ...)     struct PatchClass;     namespace dreamkurumi {         template <> struct PatchMetadata<PatchClass> {             static constexpr const char* TargetSymbol = SymbolNameStr;             static constexpr uintptr_t TargetFallbackRva = FallbackRvaVal;             static constexpr const char* TargetPattern = nullptr;             static constexpr const char* TargetSecondaryPattern = nullptr;             static constexpr HookPriority Priority = PriorityVal;         };     }     struct PatchClass : public ::dreamkurumi::DreamPatch<PatchClass, RetType(__VA_ARGS__)>

// Macro for Harmony-style patch targeting AOB signature pattern directly (Update-Proof!)
#define DREAM_HARMONY_AOB(PatchClass, SymbolNameStr, PatternStr, FallbackRvaVal, RetType, ...)     struct PatchClass;     namespace dreamkurumi {         template <> struct PatchMetadata<PatchClass> {             static constexpr const char* TargetSymbol = SymbolNameStr;             static constexpr uintptr_t TargetFallbackRva = FallbackRvaVal;             static constexpr const char* TargetPattern = PatternStr;             static constexpr const char* TargetSecondaryPattern = nullptr;             static constexpr HookPriority Priority = HookPriority::Normal;         };     }     struct PatchClass : public ::dreamkurumi::DreamPatch<PatchClass, RetType(__VA_ARGS__)>

// Macro for Harmony-style patch with Primary AND Secondary Fallback AOB (Extreme Resiliency)
#define DREAM_HARMONY_AOB_RESILIENT(PatchClass, SymbolNameStr, PrimaryPatternStr, SecondaryPatternStr, FallbackRvaVal, RetType, ...)     struct PatchClass;     namespace dreamkurumi {         template <> struct PatchMetadata<PatchClass> {             static constexpr const char* TargetSymbol = SymbolNameStr;             static constexpr uintptr_t TargetFallbackRva = FallbackRvaVal;             static constexpr const char* TargetPattern = PrimaryPatternStr;             static constexpr const char* TargetSecondaryPattern = SecondaryPatternStr;             static constexpr HookPriority Priority = HookPriority::Normal;         };     }     struct PatchClass : public ::dreamkurumi::DreamPatch<PatchClass, RetType(__VA_ARGS__)>

// Macro for Harmony-style patch with Primary AND Secondary Fallback AOB and Custom Priority
#define DREAM_HARMONY_AOB_RESILIENT_PRIORITY(PatchClass, SymbolNameStr, PrimaryPatternStr, SecondaryPatternStr, PriorityVal, FallbackRvaVal, RetType, ...)     struct PatchClass;     namespace dreamkurumi {         template <> struct PatchMetadata<PatchClass> {             static constexpr const char* TargetSymbol = SymbolNameStr;             static constexpr uintptr_t TargetFallbackRva = FallbackRvaVal;             static constexpr const char* TargetPattern = PrimaryPatternStr;             static constexpr const char* TargetSecondaryPattern = SecondaryPatternStr;             static constexpr HookPriority Priority = PriorityVal;         };     }     struct PatchClass : public ::dreamkurumi::DreamPatch<PatchClass, RetType(__VA_ARGS__)>

// Convenient AOB patch macro without requiring explicit fallback RVA
#define DREAM_HARMONY_AOB_AUTO(PatchClass, SymbolNameStr, PatternStr, RetType, ...)     DREAM_HARMONY_AOB(PatchClass, SymbolNameStr, PatternStr, 0, RetType, __VA_ARGS__)

// Explicit registration macro to bind patch to PatchAll registry
#define DREAM_REGISTER_PATCH(PatchClass)     inline auto _auto_reg_##PatchClass = &PatchClass::s_autoRegister;
