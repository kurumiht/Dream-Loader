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

extern "C" __attribute__((naked)) void DreamLongjmpThunk(jmp_buf* jb, int val) {
    asm volatile(
        ".intel_syntax noprefix\n"
        "mov rbx, [rcx + 8]\n"
        "mov rsp, [rcx + 16]\n"
        "mov rbp, [rcx + 24]\n"
        "mov rsi, [rcx + 32]\n"
        "mov rdi, [rcx + 40]\n"
        "mov r12, [rcx + 48]\n"
        "mov r13, [rcx + 56]\n"
        "mov r14, [rcx + 64]\n"
        "mov r15, [rcx + 72]\n"
        "mov r10, [rcx + 80]\n" // Rip
        "movdqu xmm6,  [rcx + 96]\n"
        "movdqu xmm7,  [rcx + 112]\n"
        "movdqu xmm8,  [rcx + 128]\n"
        "movdqu xmm9,  [rcx + 144]\n"
        "movdqu xmm10, [rcx + 160]\n"
        "movdqu xmm11, [rcx + 176]\n"
        "movdqu xmm12, [rcx + 192]\n"
        "movdqu xmm13, [rcx + 208]\n"
        "movdqu xmm14, [rcx + 224]\n"
        "movdqu xmm15, [rcx + 240]\n"
        "mov eax, edx\n"
        "test eax, eax\n"
        "jnz 1f\n"
        "mov eax, 1\n"
        "1:\n"
        "jmp r10\n"
        ".att_syntax prefix\n"
    );
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

    static size_t HashModName(const char* s) noexcept {
        uint64_t h = 14695981039346656037ULL;
        while (s && *s) { h ^= static_cast<uint8_t>(*s++); h *= 1099511628211ULL; }
        return static_cast<size_t>(h & 63);
    }

    // Check if mod circuit breaker is tripped (too many consecutive crashes) - Zero Lock, Zero Alloc
    static bool IsCircuitTripped(const char* modName) noexcept {
        if (!modName) return false;
        return Instance().m_faultCounts[HashModName(modName)].load(std::memory_order_relaxed) >= 5;
    }

    static void RecordSuccess(const char* modName) noexcept {
        if (!modName) return;
        auto& slot = Instance().m_faultCounts[HashModName(modName)];
        if (slot.load(std::memory_order_relaxed) != 0) {
            slot.store(0, std::memory_order_relaxed);
        }
    }

    static void RecordFault(const char* modName) noexcept {
        if (!modName) return;
        size_t cnt = ++Instance().m_faultCounts[HashModName(modName)];
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

            // Handle stack overflow by resetting stack guard page
            if (code == 0xC00000FD) { // EXCEPTION_STACK_OVERFLOW
                _resetstkoflw();
            }

            // DO NOT log inside VEH! Logging can deadlock if logger or heap lock was held.
            // Safely redirect context to DreamLongjmpThunk via OS NtContinue dispatcher.
            // NtContinue will unwind OS exception dispatch frames cleanly and execute thunk
            // on an aligned stack frame, preventing OS dispatcher frame abandonment.
            auto* c = pExceptionInfo->ContextRecord;
            c->Rip = (DWORD64)&DreamLongjmpThunk;
            c->Rcx = (DWORD64)&ctx->jmpBuf;
            c->Rdx = 1;
            c->Rsp = ((c->Rsp - 0x100) & ~0xFULL) - 8;
            return EXCEPTION_CONTINUE_EXECUTION;
        }

        return EXCEPTION_CONTINUE_SEARCH;
    }

    PVOID m_vehHandle{nullptr};
    std::mutex m_mutex;
    std::atomic<size_t> m_faultCounts[64]{};
    std::atomic<size_t> m_totalCrashesPrevented{0};
};

} // namespace dreamkurumi
