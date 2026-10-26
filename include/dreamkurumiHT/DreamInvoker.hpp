#pragma once
#include <windows.h>
#include <cstdint>
#include <string>
#include <type_traits>
#include <utility>
#include "DreamCache.hpp"
#include "DreamCrashGuard.hpp"
#include "DreamLog.hpp"

namespace dreamkurumi {

// ============================================================================
// DreamInvoker: Type-Safe Direct Game Function & Member Method Invoker
// Features: Perfect Forwarding, Member Function Dispatch, Virtual Table Indexing,
//           Anti-Crash Guarded Execution, Direct Pattern Calling
// ============================================================================
class DreamInvoker {
public:
    // Get free function pointer by raw address
    template <typename FnSig>
    static auto Get(void* address) {
        return reinterpret_cast<FnSig*>(address);
    }

    // Get function pointer by Symbol Cache name
    template <typename FnSig>
    static auto GetSymbol(const std::string& symbolName, uintptr_t fallbackRva = 0) {
        void* addr = DreamCache::Instance().Resolve(symbolName, fallbackRva);
        if (!addr) {
            DREAM_LOG_ERROR("DreamInvoker: Could not resolve symbol '%s'", symbolName.c_str());
        }
        return reinterpret_cast<FnSig*>(addr);
    }

    // Get virtual method pointer from instance's VTable at index
    template <typename FnSig>
    static auto GetVirtual(void* instance, size_t vtableIndex) {
        if (!instance) return static_cast<FnSig*>(nullptr);
        void** vtable = *reinterpret_cast<void***>(instance);
        if (!vtable) return static_cast<FnSig*>(nullptr);
        return reinterpret_cast<FnSig*>(vtable[vtableIndex]);
    }

    // ========================================================================
    // Free Function Invocation
    // ========================================================================
    template <typename Ret, typename... Args>
    static Ret Call(void* address, Args&&... args) {
        auto fn = Get<Ret(Args...)>(address);
        if (!fn) {
            if constexpr (!std::is_void_v<Ret>) return Ret{};
            else return;
        }
        return fn(std::forward<Args>(args)...);
    }

    template <typename Ret, typename... Args>
    static Ret CallSymbol(const std::string& symbolName, uintptr_t fallbackRva, Args&&... args) {
        auto fn = GetSymbol<Ret(Args...)>(symbolName, fallbackRva);
        if (!fn) {
            if constexpr (!std::is_void_v<Ret>) return Ret{};
            else return;
        }
        return fn(std::forward<Args>(args)...);
    }

    // Call by pattern directly (auto-scanned via AVX2 DreamScanner & cached)
    template <typename Ret, typename... Args>
    static Ret CallPattern(const std::string& symbolName, const std::string& patternStr, uintptr_t fallbackRva, Args&&... args) {
        void* addr = DreamCache::Instance().ResolvePattern(symbolName, patternStr, fallbackRva);
        if (!addr) {
            DREAM_LOG_ERROR("DreamInvoker: Could not resolve pattern for '%s'", symbolName.c_str());
            if constexpr (!std::is_void_v<Ret>) return Ret{};
            else return;
        }
        return Call<Ret>(addr, std::forward<Args>(args)...);
    }

    // ========================================================================
    // Member Function & Method Invocation (Passes 'instance' as 'this' in RCX)
    // ========================================================================
    template <typename Ret, typename InstanceType, typename... Args>
    static Ret CallMember(InstanceType* instance, void* memberFuncAddress, Args&&... args) {
        if (!instance || !memberFuncAddress) {
            if constexpr (!std::is_void_v<Ret>) return Ret{};
            else return;
        }
        // In x64 ABI, 'this' pointer is passed as the first parameter (RCX)
        using MemberFn = Ret(*)(InstanceType*, Args...);
        auto fn = reinterpret_cast<MemberFn>(memberFuncAddress);
        return fn(instance, std::forward<Args>(args)...);
    }

    template <typename Ret, typename InstanceType, typename... Args>
    static Ret CallMemberSymbol(InstanceType* instance, const std::string& symbolName, uintptr_t fallbackRva, Args&&... args) {
        void* addr = DreamCache::Instance().Resolve(symbolName, fallbackRva);
        return CallMember<Ret>(instance, addr, std::forward<Args>(args)...);
    }

    // ========================================================================
    // Virtual Method Invocation by VTable Index
    // ========================================================================
    template <typename Ret, typename InstanceType, typename... Args>
    static Ret CallVirtual(InstanceType* instance, size_t vtableIndex, Args&&... args) {
        if (!instance) {
            if constexpr (!std::is_void_v<Ret>) return Ret{};
            else return;
        }
        void** vtable = *reinterpret_cast<void***>(instance);
        if (!vtable) {
            if constexpr (!std::is_void_v<Ret>) return Ret{};
            else return;
        }
        using VirtualFn = Ret(*)(InstanceType*, Args...);
        auto fn = reinterpret_cast<VirtualFn>(vtable[vtableIndex]);
        if (!fn) {
            if constexpr (!std::is_void_v<Ret>) return Ret{};
            else return;
        }
        return fn(instance, std::forward<Args>(args)...);
    }

    // ========================================================================
    // Anti-Crash Guarded Invocation (Protected by DreamCrashGuard)
    // If the game function faults or encounters null pointers, returns fallback
    // ========================================================================
    template <typename Ret, typename... Args>
    requires (!std::is_void_v<Ret>)
    static Ret CallSafe(void* address, Ret fallbackValue, Args&&... args) {
        if (!address) return fallbackValue;
        return DreamCrashGuard::Execute("GameFuncCall", [&]() -> Ret {
            return Call<Ret>(address, std::forward<Args>(args)...);
        }, [&]() -> Ret {
            DREAM_LOG_WARN("DreamInvoker: Crash suppressed during CallSafe at 0x%p", address);
            return fallbackValue;
        });
    }

    template <typename Ret = void, typename... Args>
    requires std::is_void_v<Ret>
    static void CallSafe(void* address, Args&&... args) {
        if (!address) return;
        DreamCrashGuard::ExecuteSafely("GameFuncCallVoid", [&]() {
            Call<void>(address, std::forward<Args>(args)...);
        });
    }

    template <typename Ret, typename InstanceType, typename... Args>
    requires (!std::is_void_v<Ret>)
    static Ret CallMemberSafe(InstanceType* instance, void* memberFuncAddress, Ret fallbackValue, Args&&... args) {
        if (!instance || !memberFuncAddress) return fallbackValue;
        return DreamCrashGuard::Execute("MemberMethodCall", [&]() -> Ret {
            return CallMember<Ret>(instance, memberFuncAddress, std::forward<Args>(args)...);
        }, [&]() -> Ret {
            DREAM_LOG_WARN("DreamInvoker: Crash suppressed during CallMemberSafe at 0x%p", memberFuncAddress);
            return fallbackValue;
        });
    }

    template <typename Ret = void, typename InstanceType, typename... Args>
    requires std::is_void_v<Ret>
    static void CallMemberSafe(InstanceType* instance, void* memberFuncAddress, Args&&... args) {
        if (!instance || !memberFuncAddress) return;
        DreamCrashGuard::ExecuteSafely("MemberMethodCallVoid", [&]() {
            CallMember<void>(instance, memberFuncAddress, std::forward<Args>(args)...);
        });
    }

    template <typename Ret, typename InstanceType, typename... Args>
    requires (!std::is_void_v<Ret>)
    static Ret CallVirtualSafe(InstanceType* instance, size_t vtableIndex, Ret fallbackValue, Args&&... args) {
        if (!instance) return fallbackValue;
        return DreamCrashGuard::Execute("VirtualMethodCall", [&]() -> Ret {
            return CallVirtual<Ret>(instance, vtableIndex, std::forward<Args>(args)...);
        }, [&]() -> Ret {
            DREAM_LOG_WARN("DreamInvoker: Crash suppressed during CallVirtualSafe at VTable index %zu", vtableIndex);
            return fallbackValue;
        });
    }

    template <typename Ret = void, typename InstanceType, typename... Args>
    requires std::is_void_v<Ret>
    static void CallVirtualSafe(InstanceType* instance, size_t vtableIndex, Args&&... args) {
        if (!instance) return;
        DreamCrashGuard::ExecuteSafely("VirtualMethodCallVoid", [&]() {
            CallVirtual<void>(instance, vtableIndex, std::forward<Args>(args)...);
        });
    }
};

} // namespace dreamkurumi
