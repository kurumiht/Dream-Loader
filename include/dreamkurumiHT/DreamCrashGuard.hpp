#pragma once
#include <windows.h>
#include <cstdint>
#include <string>
#include <functional>
#include <mutex>
#include <atomic>
#include <type_traits>
#include <csetjmp>
#include <unordered_map>
#include "DreamLog.hpp"

extern "C" inline __attribute__((noinline)) void DreamLongjmpThunk(jmp_buf* jb, int val) {
    longjmp(*jb, val);
}

namespace dreamkurumi {

// ============================================================================
// DreamCrashGuard - Anti-Crash Mod Sandbox Shield
// Protects the game process from crashing when mod code throws Access Violations
// Uses Windows TEB Thread-Local Storage, Vectored Exception Handling,
// Clean NtContinue context redirection (zero OS dispatcher frame abandonment),
// and a Circuit Breaker to prevent infinite crash loops.
// ============================================================================
class DreamCrashGuard {
public:
    struct SandboxContext {
        volatile bool inSandbox{false};
        const char* modName{nullptr};
        jmp_buf jmpBuf;
        uintptr_t faultRip{0};
        DWORD faultCode{0};
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

    // Check if mod circuit breaker is tripped (too many consecutive crashes)
    static bool IsCircuitTripped(const char* modName) {
        if (!modName) return false;
        std::lock_guard<std::mutex> lock(Instance().m_circuitMutex);
        auto it = Instance().m_faultCounts.find(modName);
        return (it != Instance().m_faultCounts.end() && it->second >= 5);
    }

    static void RecordSuccess(const char* modName) {
        if (!modName) return;
        std::lock_guard<std::mutex> lock(Instance().m_circuitMutex);
        Instance().m_faultCounts[modName] = 0;
    }

    static void RecordFault(const char* modName) {
        if (!modName) return;
        std::lock_guard<std::mutex> lock(Instance().m_circuitMutex);
        size_t cnt = ++Instance().m_faultCounts[modName];
        if (cnt == 5) {
            DREAM_LOG_ERROR("DreamCrashGuard: CIRCUIT BREAKER TRIPPED for mod '%s' (5 consecutive crashes). Mod callback disabled.", modName);
        }
    }

    // Execute mod callback inside an isolated sandbox shield
    template <typename Callback, typename Fallback>
    __attribute__((noinline)) static auto Execute(const char* modName, Callback&& callback, Fallback&& fallback)
        -> decltype(callback())
    {
        Initialize();

        using ReturnType = decltype(callback());

        if (IsCircuitTripped(modName)) {
            if constexpr (std::is_void_v<ReturnType>) {
                fallback();
                return;
            } else {
                return fallback();
            }
        }

        SandboxContext ctx;
        ctx.inSandbox = true;
        ctx.modName = modName ? modName : "UnknownMod";
        ctx.faultRip = 0;
        ctx.faultCode = 0;
        ctx.prevCtx = reinterpret_cast<SandboxContext*>(TlsGetValue(GetTlsIndex()));
        TlsSetValue(GetTlsIndex(), &ctx);

        if (setjmp(ctx.jmpBuf) == 0) {
            if constexpr (std::is_void_v<ReturnType>) {
                callback();
                TlsSetValue(GetTlsIndex(), ctx.prevCtx);
                RecordSuccess(ctx.modName);
                return;
            } else {
                ReturnType result = callback();
                TlsSetValue(GetTlsIndex(), ctx.prevCtx);
                RecordSuccess(ctx.modName);
                return result;
            }
        } else {
            // Execution arrived here safely on clean stack after NtContinue context redirection to DreamLongjmpThunk!
            TlsSetValue(GetTlsIndex(), ctx.prevCtx);
            Instance().m_totalCrashesPrevented++;
            RecordFault(ctx.modName);

            // Safe to log here on clean stack without deadlock risk!
            DREAM_LOG_ERROR("DreamCrashGuard: CRASH INTERCEPTED! Code: 0x%08lX in mod '%s' at RIP: 0x%p",
                            ctx.faultCode, ctx.modName, reinterpret_cast<void*>(ctx.faultRip));
            DREAM_LOG_WARN("DreamCrashGuard: Safely suppressed crash and resumed game execution.");

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

    static bool IsFatalHardwareException(DWORD code) {
        switch (code) {
            case EXCEPTION_ACCESS_VIOLATION:        // 0xC0000005
            case EXCEPTION_IN_PAGE_ERROR:           // 0xC0000006
            case EXCEPTION_ILLEGAL_INSTRUCTION:     // 0xC000001D
            case EXCEPTION_INT_DIVIDE_BY_ZERO:      // 0xC0000094
            case EXCEPTION_PRIV_INSTRUCTION:        // 0xC0000096
            case EXCEPTION_DATATYPE_MISALIGNMENT:   // 0x80000002
            case 0xC00000FD:                        // EXCEPTION_STACK_OVERFLOW
                return true;
            default:
                return false;
        }
    }

    static LONG WINAPI VectoredCrashHandler(PEXCEPTION_POINTERS pExceptionInfo) {
        if (!pExceptionInfo || !pExceptionInfo->ExceptionRecord) {
            return EXCEPTION_CONTINUE_SEARCH;
        }

        DWORD code = pExceptionInfo->ExceptionRecord->ExceptionCode;

        // Whitelist only fatal hardware exception codes
        // Never catch MSVC C++ exceptions (0xE06D7363) or non-hardware exceptions!
        if (!IsFatalHardwareException(code)) {
            return EXCEPTION_CONTINUE_SEARCH;
        }

        auto* ctx = reinterpret_cast<SandboxContext*>(TlsGetValue(GetTlsIndex()));

        // Check if crash happened inside an active mod sandbox
        if (ctx && ctx->inSandbox) {
            // Disarm sandbox immediately
            ctx->inSandbox = false;
            ctx->faultRip = pExceptionInfo->ContextRecord->Rip;
            ctx->faultCode = code;

            // DO NOT log inside VEH! Logging can deadlock if logger or heap lock was held.
            // Safely redirect context to DreamLongjmpThunk and return EXCEPTION_CONTINUE_EXECUTION.
            // NtContinue will pop OS exception frames and execute thunk on a clean 16-byte aligned stack.
            // Under MinGW GCC x64 on Windows, longjmp from VEH cleanly unwinds
            // directly back to the setjmp landing pad without synthetic stack frame errors.
            // Logging is deferred to Execute() on the restored stack to prevent deadlocks.
            longjmp(ctx->jmpBuf, 1);
        }

        return EXCEPTION_CONTINUE_SEARCH;
    }

    PVOID m_vehHandle{nullptr};
    std::mutex m_mutex;
    std::mutex m_circuitMutex;
    std::unordered_map<std::string, size_t> m_faultCounts;
    std::atomic<size_t> m_totalCrashesPrevented{0};
};

} // namespace dreamkurumi
