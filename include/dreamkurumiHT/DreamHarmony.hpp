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
};

// ============================================================================
// Central Harmony Registry (Manages PatchAll & UnpatchAll)
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
                DREAM_LOG_INFO("  [OK] Installed patch: %s", p->GetName());
                count++;
            } else {
                DREAM_LOG_WARN("  [FAIL] Could not install patch: %s", p->GetName());
            }
        }
        DREAM_LOG_INFO("DreamHarmony: PatchAll complete (%zu/%zu succeeded)", count, m_patches.size());
        return count;
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
        AutoRegister() {
            DreamHarmonyRegistry::Instance().Register(this);
        }

        const char* GetName() const override {
            if constexpr (PatchMetadata<Derived>::TargetSymbol != nullptr) {
                return PatchMetadata<Derived>::TargetSymbol;
            }
            return "UnnamedPatch";
        }

        bool InstallPatch() override {
            if constexpr (PatchMetadata<Derived>::TargetSymbol != nullptr) {
                uintptr_t fallback = PatchMetadata<Derived>::TargetFallbackRva;
                const char* pattern = PatchMetadata<Derived>::TargetPattern;

                if (pattern) {
                    void* addr = DreamCache::Instance().ResolvePattern(PatchMetadata<Derived>::TargetSymbol, pattern, fallback);
                    return Install(addr);
                } else {
                    return InstallSymbol(PatchMetadata<Derived>::TargetSymbol, fallback);
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
        }; \
    } \
    struct PatchClass : public ::dreamkurumi::DreamPatch<PatchClass, RetType(__VA_ARGS__)>

// Convenient AOB patch macro without requiring explicit fallback RVA
#define DREAM_HARMONY_AOB_AUTO(PatchClass, SymbolNameStr, PatternStr, RetType, ...) \
    DREAM_HARMONY_AOB(PatchClass, SymbolNameStr, PatternStr, 0, RetType, __VA_ARGS__)

// Explicit registration macro to bind patch to PatchAll registry
#define DREAM_REGISTER_PATCH(PatchClass) \
    inline auto _auto_reg_##PatchClass = &PatchClass::s_autoRegister;
