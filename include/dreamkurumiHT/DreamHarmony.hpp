#pragma once
#include <type_traits>
#include <vector>
#include <string>
#include "DreamHook.hpp"
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
// Harmony Patch Base Class Template
// ============================================================================
template <typename Derived, typename Ret, typename... Args>
class DreamPatchBase {
public:
    using ReturnType = Ret;
    using TargetFn = Ret(*)(Args...);

    static DreamInlineHook& GetHook() {
        static DreamInlineHook s_hook;
        return s_hook;
    }

    // Call the original unhooked function
    static Ret CallOriginal(Args... args) {
        auto orig = reinterpret_cast<TargetFn>(GetHook().GetOriginal());
        if constexpr (std::is_void_v<Ret>) {
            orig(args...);
        } else {
            return orig(args...);
        }
    }

    // Central dispatcher invoked by CPU when hooked function is called
    static Ret HookDispatcher(Args... args) {
        // 1. Run Prefix with DreamCrashGuard Sandbox
        bool runOriginal = true;
        DreamCrashGuard::ExecuteSafely("HarmonyPrefix", [&]() {
            if constexpr (requires { { Derived::Prefix(args...) } -> std::same_as<bool>; }) {
                runOriginal = Derived::Prefix(args...);
            } else if constexpr (requires { Derived::Prefix(args...); }) {
                Derived::Prefix(args...);
                runOriginal = true;
            }
        });

        if constexpr (std::is_void_v<Ret>) {
            if (runOriginal) {
                CallOriginal(args...);
            }
            // Run Postfix for void return with DreamCrashGuard Sandbox
            DreamCrashGuard::ExecuteSafely("HarmonyPostfix", [&]() {
                if constexpr (requires { Derived::Postfix(args...); }) {
                    Derived::Postfix(args...);
                }
            });
        } else {
            Ret result{};
            if (runOriginal) {
                result = CallOriginal(args...);
            }
            // Run Postfix for non-void return with DreamCrashGuard Sandbox
            DreamCrashGuard::ExecuteSafely("HarmonyPostfix", [&]() {
                if constexpr (requires { Derived::Postfix(result, args...); }) {
                    Derived::Postfix(result, args...);
                } else if constexpr (requires { Derived::Postfix(args...); }) {
                    Derived::Postfix(args...);
                }
            });
            return result;
        }
    }

    // Install by raw memory address
    static bool Install(void* targetAddress) {
        if (!targetAddress) return false;
        return GetHook().Install(targetAddress, reinterpret_cast<void*>(HookDispatcher));
    }

    // Install by Symbol Cache Name (with optional fallback RVA)
    static bool InstallSymbol(const std::string& symbolName, uintptr_t fallbackRva = 0) {
        void* addr = DreamCache::Instance().Resolve(symbolName, fallbackRva);
        if (!addr) return false;
        return Install(addr);
    }

    static bool Disable() {
        return GetHook().Disable();
    }

    static bool Enable() {
        return GetHook().Enable();
    }

    static bool IsInstalled() {
        return GetHook().IsInstalled();
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

                // 1. If pattern is specified: Try Primary AOB Pattern
                if (primaryPattern) {
                    addr = DreamCache::Instance().ResolvePattern(PatchMetadata<Derived>::TargetSymbol, primaryPattern, 0);
                    if (addr) {
                        m_resolutionMethod = "Primary AOB";
                    }
                }

                // 2. Try Secondary Fallback AOB Pattern if primary failed
                if (!addr && secondaryPattern) {
                    std::string secSymbol = std::string(PatchMetadata<Derived>::TargetSymbol) + "_Sec";
                    addr = DreamCache::Instance().ResolvePattern(secSymbol, secondaryPattern, 0);
                    if (addr) {
                        m_resolutionMethod = "Secondary AOB (Fallback)";
                        DREAM_LOG_WARN("DreamHarmony: Patch '%s' resolved via Secondary Fallback AOB pattern!", PatchMetadata<Derived>::TargetSymbol);
                    }
                }

                // 3. Try Symbol Cache or Fallback Direct RVA
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
            return GetHook().IsInstalled();
        }
    };

    inline static AutoRegister s_autoRegister{};
    inline static const void* _forceInit = &s_autoRegister;
};

// Convenient template alias
template <typename Derived, typename FnSig>
class DreamPatch;

template <typename Derived, typename Ret, typename... Args>
class DreamPatch<Derived, Ret(Args...)> : public DreamPatchBase<Derived, Ret, Args...> {};

} // namespace dreamkurumi

// Macro for basic manual patch declarations
#define DREAM_DECLARE_PATCH(PatchClass, RetType, ...) \
    struct PatchClass : public ::dreamkurumi::DreamPatch<PatchClass, RetType(__VA_ARGS__)>

// Macro for full Harmony-style auto-registering patches (supports PatchAll!)
#define DREAM_HARMONY_PATCH(PatchClass, SymbolNameStr, FallbackRvaVal, RetType, ...) \
    struct PatchClass; \
    namespace dreamkurumi { \
        template <> struct PatchMetadata<PatchClass> { \
            static constexpr const char* TargetSymbol = SymbolNameStr; \
            static constexpr uintptr_t TargetFallbackRva = FallbackRvaVal; \
            static constexpr const char* TargetPattern = nullptr; \
            static constexpr const char* TargetSecondaryPattern = nullptr; \
        }; \
    } \
    struct PatchClass : public ::dreamkurumi::DreamPatch<PatchClass, RetType(__VA_ARGS__)>

// Macro for Harmony-style patch targeting AOB signature pattern directly (Update-Proof!)
#define DREAM_HARMONY_AOB(PatchClass, SymbolNameStr, PatternStr, FallbackRvaVal, RetType, ...) \
    struct PatchClass; \
    namespace dreamkurumi { \
        template <> struct PatchMetadata<PatchClass> { \
            static constexpr const char* TargetSymbol = SymbolNameStr; \
            static constexpr uintptr_t TargetFallbackRva = FallbackRvaVal; \
            static constexpr const char* TargetPattern = PatternStr; \
            static constexpr const char* TargetSecondaryPattern = nullptr; \
        }; \
    } \
    struct PatchClass : public ::dreamkurumi::DreamPatch<PatchClass, RetType(__VA_ARGS__)>

// Macro for Harmony-style patch with Primary AND Secondary Fallback AOB (Extreme Resiliency)
#define DREAM_HARMONY_AOB_RESILIENT(PatchClass, SymbolNameStr, PrimaryPatternStr, SecondaryPatternStr, FallbackRvaVal, RetType, ...) \
    struct PatchClass; \
    namespace dreamkurumi { \
        template <> struct PatchMetadata<PatchClass> { \
            static constexpr const char* TargetSymbol = SymbolNameStr; \
            static constexpr uintptr_t TargetFallbackRva = FallbackRvaVal; \
            static constexpr const char* TargetPattern = PrimaryPatternStr; \
            static constexpr const char* TargetSecondaryPattern = SecondaryPatternStr; \
        }; \
    } \
    struct PatchClass : public ::dreamkurumi::DreamPatch<PatchClass, RetType(__VA_ARGS__)>

// Convenient AOB patch macro without requiring explicit fallback RVA
#define DREAM_HARMONY_AOB_AUTO(PatchClass, SymbolNameStr, PatternStr, RetType, ...) \
    DREAM_HARMONY_AOB(PatchClass, SymbolNameStr, PatternStr, 0, RetType, __VA_ARGS__)

// Explicit registration macro to bind patch to PatchAll registry
#define DREAM_REGISTER_PATCH(PatchClass) \
    inline auto _auto_reg_##PatchClass = &PatchClass::s_autoRegister;
