#pragma once
#include <windows.h>
#include <cstdint>
#include <string>
#include <functional>
#include <mutex>
#include <atomic>
#include <type_traits>
#include "DreamLog.hpp"

namespace dreamkurumi {

// ============================================================================
// DreamCrashGuard - Anti-Crash Mod Sandbox Shield
// Protects the game process from crashing when mod code throws Access Violations
// Uses Windows TEB Thread-Local Storage and Vectored Exception Handling
// ============================================================================
class DreamCrashGuard {
public:
    struct SandboxContext {
        volatile bool inSandbox{false};
        const char* modName{nullptr};
        void* jmpBuf[5]{nullptr, nullptr, nullptr, nullptr, nullptr};
        SandboxContext* prevCtx{nullptr};
    };

    static DreamCrashGuard& Instance() {
        static DreamCrashGuard s_instance;
        return s_instance;
    }

    static DWORD GetTlsIndex() {
        static DWORD s_tls = TlsAlloc();
        return s_tls;
    }

    // Initialize Vectored Exception Handler for the Crash Guard (Zero lock overhead on hot path)
    static void Initialize() {
        static bool s_alreadyInit = false;
        if (s_alreadyInit) return;
        std::lock_guard<std::mutex> lock(Instance().m_mutex);
        if (!Instance().m_vehHandle) {
            Instance().m_vehHandle = AddVectoredExceptionHandler(1, VectoredCrashHandler);
            if (Instance().m_vehHandle) {
                DREAM_LOG_INFO("DreamCrashGuard: Vectored Exception Handler installed successfully");
            }
            s_alreadyInit = true;
        }
    }

    // Unregister VEH handler on shutdown
    static void Shutdown() {
        std::lock_guard<std::mutex> lock(Instance().m_mutex);
        if (Instance().m_vehHandle) {
            RemoveVectoredExceptionHandler(Instance().m_vehHandle);
            Instance().m_vehHandle = nullptr;
        }
    }

    // Execute mod callback inside an isolated sandbox shield
    template <typename Callback, typename Fallback>
    __attribute__((noinline)) static auto Execute(const char* modName, Callback&& callback, Fallback&& fallback)
        -> decltype(callback())
    {
        Initialize();

        SandboxContext ctx;
        ctx.inSandbox = true;
        ctx.modName = modName ? modName : "UnknownMod";
        ctx.prevCtx = reinterpret_cast<SandboxContext*>(TlsGetValue(GetTlsIndex()));
        TlsSetValue(GetTlsIndex(), &ctx);

        using ReturnType = decltype(callback());

        if (__builtin_setjmp(ctx.jmpBuf) == 0) {
            if constexpr (std::is_void_v<ReturnType>) {
                callback();
                TlsSetValue(GetTlsIndex(), ctx.prevCtx);
                return;
            } else {
                ReturnType result = callback();
                TlsSetValue(GetTlsIndex(), ctx.prevCtx);
                return result;
            }
        } else {
            // Execution arrived here safely via VEH redirection upon intercepted fault
            TlsSetValue(GetTlsIndex(), ctx.prevCtx);
            Instance().m_totalCrashesPrevented++;

            if constexpr (std::is_void_v<ReturnType>) {
                fallback();
                return;
            } else {
                return fallback();
            }
        }
    }

    // Simple void execution overload
    template <typename Callback>
    __attribute__((noinline)) static bool ExecuteSafely(const char* modName, Callback&& callback) {
        bool succeeded = true;
        Execute(modName, [&]() {
            callback();
        }, [&]() {
            succeeded = false;
        });
        return succeeded;
    }

    size_t GetTotalCrashesPrevented() const {
        return m_totalCrashesPrevented;
    }

private:
    DreamCrashGuard() = default;
    ~DreamCrashGuard() { Shutdown(); }

    static LONG WINAPI VectoredCrashHandler(PEXCEPTION_POINTERS pExceptionInfo) {
        if (!pExceptionInfo || !pExceptionInfo->ExceptionRecord) {
            return EXCEPTION_CONTINUE_SEARCH;
        }

        DWORD code = pExceptionInfo->ExceptionRecord->ExceptionCode;

        // Only intercept Fatal Error codes (Severity bits 31:30 == 11b, i.e. 0xC0000005)
        // Discard Informational (0x4..., e.g. OutputDebugString 0x40010006) and Warnings (0x8..., e.g. Single Step)
        if ((code & 0xC0000000) != 0xC0000000) {
            return EXCEPTION_CONTINUE_SEARCH;
        }

        auto* ctx = reinterpret_cast<SandboxContext*>(TlsGetValue(GetTlsIndex()));

        // Check if crash happened inside an active mod sandbox
        if (ctx && ctx->inSandbox && ctx->jmpBuf[1]) {
            // Disarm sandbox immediately to prevent any re-entry during logging
            ctx->inSandbox = false;

            const char* modName = ctx->modName ? ctx->modName : "UnknownMod";
            void* faultAddress = reinterpret_cast<void*>(pExceptionInfo->ContextRecord->Rip);

            DREAM_LOG_ERROR("DreamCrashGuard: CRASH INTERCEPTED! Code: 0x%08lX in mod '%s' at RIP: 0x%p",
                            code, modName, faultAddress);
            DREAM_LOG_WARN("DreamCrashGuard: Safely suppressing crash and resuming normal game execution...");

            // Redirect thread execution context to safe landing pad and restore stack via compiler setjmp frame
            pExceptionInfo->ContextRecord->Rip = reinterpret_cast<DWORD64>(ctx->jmpBuf[1]);
            pExceptionInfo->ContextRecord->Rsp = reinterpret_cast<DWORD64>(ctx->jmpBuf[2]);
            pExceptionInfo->ContextRecord->Rbp = reinterpret_cast<DWORD64>(ctx->jmpBuf[0]);

            return EXCEPTION_CONTINUE_EXECUTION;
        }

        return EXCEPTION_CONTINUE_SEARCH;
    }

    PVOID m_vehHandle{nullptr};
    std::mutex m_mutex;
    std::atomic<size_t> m_totalCrashesPrevented{0};
};

} // namespace dreamkurumi
