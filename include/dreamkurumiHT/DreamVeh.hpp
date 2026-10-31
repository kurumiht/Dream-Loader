#pragma once
#include <windows.h>
#include <tlhelp32.h>
#include <cstdint>
#include <functional>
#include <unordered_map>
#include <mutex>
#include "DreamHook.hpp"
#include "DreamLog.hpp"

#ifndef NTSTATUS
typedef LONG NTSTATUS;
#endif
#ifndef NTAPI
#define NTAPI __stdcall
#endif
#ifndef PFN_NT_GET_NEXT_THREAD_DEFINED
#define PFN_NT_GET_NEXT_THREAD_DEFINED
typedef NTSTATUS(NTAPI* pfnNtGetNextThread)(HANDLE ProcessHandle, HANDLE ThreadHandle, ACCESS_MASK DesiredAccess, ULONG HandleAttributes, ULONG Flags, PHANDLE NewThreadHandle);
#endif

namespace dreamkurumi {

enum class BreakOn : uint8_t {
    Execute   = 0, // Trigger on instruction execution
    Write     = 1, // Trigger on memory write
    ReadWrite = 3  // Trigger on memory read or write
};

enum class BreakSize : uint8_t {
    Bytes1 = 0, // 00
    Bytes2 = 1, // 01
    Bytes8 = 2, // 10 (x86_64)
    Bytes4 = 3  // 11
};

struct HardwareBreakpointInfo {
    std::atomic<void*> address{nullptr};
    std::atomic<BreakOn> condition{BreakOn::Execute};
    std::atomic<BreakSize> size{BreakSize::Bytes1};
    int slot{-1}; // DR0..DR3 (0..3)
    std::function<void(DreamCpuContext&)> callback;
    std::atomic<bool> active{false};

    HardwareBreakpointInfo() = default;
    HardwareBreakpointInfo(const HardwareBreakpointInfo& other) {
        address.store(other.address.load(std::memory_order_relaxed), std::memory_order_relaxed);
        condition.store(other.condition.load(std::memory_order_relaxed), std::memory_order_relaxed);
        size.store(other.size.load(std::memory_order_relaxed), std::memory_order_relaxed);
        slot = other.slot;
        callback = other.callback;
        active.store(other.active.load(std::memory_order_relaxed), std::memory_order_relaxed);
    }
    HardwareBreakpointInfo& operator=(const HardwareBreakpointInfo& other) {
        if (this != &other) {
            address.store(other.address.load(std::memory_order_relaxed), std::memory_order_relaxed);
            condition.store(other.condition.load(std::memory_order_relaxed), std::memory_order_relaxed);
            size.store(other.size.load(std::memory_order_relaxed), std::memory_order_relaxed);
            slot = other.slot;
            callback = other.callback;
            active.store(other.active.load(std::memory_order_relaxed), std::memory_order_relaxed);
        }
        return *this;
    }
};

class DreamVeh {
public:
    static DreamVeh& Instance() {
        static DreamVeh s_instance;
        return s_instance;
    }

    bool Initialize() {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_vehHandle) return true;

        m_vehHandle = AddVectoredExceptionHandler(1, VectoredExceptionHandler);
        if (!m_vehHandle) {
            DREAM_LOG_ERROR("DreamVeh: Failed to register Vectored Exception Handler");
            return false;
        }

        DREAM_LOG_INFO("DreamVeh: Hardware Breakpoint VEH registered successfully");
        return true;
    }

    void Shutdown() {
        std::lock_guard<std::mutex> lock(m_mutex);
        ClearAllBreakpoints();
        if (m_vehHandle) {
            RemoveVectoredExceptionHandler(m_vehHandle);
            m_vehHandle = nullptr;
        }
    }

    // Set a Hardware Breakpoint on an address
    // Slot is automatically assigned to DR0, DR1, DR2, or DR3
    int SetBreakpoint(void* address, BreakOn condition, BreakSize size, std::function<void(DreamCpuContext&)> callback) {
        if (!address) {
            DREAM_LOG_ERROR("DreamVeh: Cannot set breakpoint on null address");
            return -1;
        }

        // x86/x64 debug registers: Execute breakpoints MUST use size 1
        if (condition == BreakOn::Execute && size != BreakSize::Bytes1) {
            DREAM_LOG_WARN("DreamVeh: Execute breakpoints must have 1-byte length; enforcing Bytes1");
            size = BreakSize::Bytes1;
        }

        // Validate address alignment required by hardware debug registers
        uintptr_t addrVal = reinterpret_cast<uintptr_t>(address);
        if (size == BreakSize::Bytes2 && (addrVal & 1) != 0) {
            DREAM_LOG_ERROR("DreamVeh: 2-byte breakpoint address 0x%p must be 2-byte aligned", address);
            return -1;
        }
        if (size == BreakSize::Bytes4 && (addrVal & 3) != 0) {
            DREAM_LOG_ERROR("DreamVeh: 4-byte breakpoint address 0x%p must be 4-byte aligned", address);
            return -1;
        }
        if (size == BreakSize::Bytes8 && (addrVal & 7) != 0) {
            DREAM_LOG_ERROR("DreamVeh: 8-byte breakpoint address 0x%p must be 8-byte aligned", address);
            return -1;
        }

        if (!m_vehHandle) {
            if (!Initialize()) return -1;
        }

        std::lock_guard<std::mutex> lock(m_mutex);

        int slot = -1;
        for (int i = 0; i < 4; ++i) {
            if (!m_breakpoints[i].active.load(std::memory_order_acquire)) {
                slot = i;
                break;
            }
        }

        if (slot == -1) {
            DREAM_LOG_WARN("DreamVeh: All 4 hardware breakpoint slots (DR0-DR3) are in use");
            return -1;
        }

        m_breakpoints[slot].address.store(address, std::memory_order_relaxed);
        m_breakpoints[slot].condition.store(condition, std::memory_order_relaxed);
        m_breakpoints[slot].size.store(size, std::memory_order_relaxed);
        m_breakpoints[slot].slot = slot;
        m_breakpoints[slot].callback = callback;
        m_breakpoints[slot].active.store(true, std::memory_order_release);

        // Apply breakpoint across all threads (including current thread via kernel context switch)
        ApplyBreakpointToAllThreads(slot, address, condition, size);

        DREAM_LOG_INFO("DreamVeh: Set Hardware Breakpoint in DR%d at 0x%p (Condition: %d)", slot, address, static_cast<int>(condition));
        return slot;
    }

    bool RemoveBreakpoint(int slot) {
        if (slot < 0 || slot > 3) return false;
        std::lock_guard<std::mutex> lock(m_mutex);

        if (!m_breakpoints[slot].active.load(std::memory_order_acquire)) return false;

        m_breakpoints[slot].active.store(false, std::memory_order_release);
        m_breakpoints[slot].address.store(nullptr, std::memory_order_release);
        m_breakpoints[slot].callback = nullptr;

        ClearBreakpointOnAllThreads(slot);
        DREAM_LOG_INFO("DreamVeh: Cleared Hardware Breakpoint DR%d", slot);
        return true;
    }

    void ClearAllBreakpoints() {
        for (int i = 0; i < 4; ++i) {
            if (m_breakpoints[i].active.load(std::memory_order_acquire)) {
                m_breakpoints[i].active.store(false, std::memory_order_release);
                m_breakpoints[i].address.store(nullptr, std::memory_order_release);
                m_breakpoints[i].callback = nullptr;
                ClearBreakpointOnAllThreads(i);
            }
        }
    }

    // Broadcast hardware breakpoint to all worker threads (alias for SetBreakpoint)
    int BroadcastBreakpoint(void* address, BreakOn condition, BreakSize size, std::function<void(DreamCpuContext&)> callback) {
        return SetBreakpoint(address, condition, size, callback);
    }

    // Apply all active breakpoints to the calling thread (called on DLL_THREAD_ATTACH)
    void ApplyToCurrentThread() {
        DWORD tid = GetCurrentThreadId();
        std::lock_guard<std::mutex> lock(m_mutex);
        for (int slot = 0; slot < 4; ++slot) {
            if (m_breakpoints[slot].active.load(std::memory_order_relaxed) && m_breakpoints[slot].address.load(std::memory_order_relaxed)) {
                ApplyBreakpointToThread(tid, slot, m_breakpoints[slot].address.load(std::memory_order_relaxed), m_breakpoints[slot].condition.load(std::memory_order_relaxed), m_breakpoints[slot].size.load(std::memory_order_relaxed));
            }
        }
    }

    // Manually synchronize active breakpoints to all currently running worker threads
    void SyncAllThreads() {
        std::lock_guard<std::mutex> lock(m_mutex);
        for (int slot = 0; slot < 4; ++slot) {
            if (m_breakpoints[slot].active.load(std::memory_order_relaxed)) {
                ApplyBreakpointToAllThreads(slot, m_breakpoints[slot].address.load(std::memory_order_relaxed), m_breakpoints[slot].condition.load(std::memory_order_relaxed), m_breakpoints[slot].size.load(std::memory_order_relaxed));
            }
        }
        DREAM_LOG_INFO("DreamVeh: Synchronized active hardware breakpoints to all process threads");
    }

private:
    DreamVeh() = default;
    ~DreamVeh() { Shutdown(); }

    static LONG WINAPI VectoredExceptionHandler(PEXCEPTION_POINTERS pExceptionInfo) {
        if (!pExceptionInfo || !pExceptionInfo->ExceptionRecord) {
            return EXCEPTION_CONTINUE_SEARCH;
        }

        DWORD code = pExceptionInfo->ExceptionRecord->ExceptionCode;

        // Hardware Breakpoint Interception
        if (code == EXCEPTION_SINGLE_STEP || code == STATUS_SINGLE_STEP) {
            auto& inst = DreamVeh::Instance();
            DWORD64 dr6 = pExceptionInfo->ContextRecord->Dr6;

            bool handled = false;
            bool hasExecuteBp = false;
            for (int i = 0; i < 4; ++i) {
                if (!inst.m_breakpoints[i].active.load(std::memory_order_acquire)) continue;

                void* bpAddr = inst.m_breakpoints[i].address.load(std::memory_order_acquire);
                BreakOn bpCond = inst.m_breakpoints[i].condition.load(std::memory_order_relaxed);

                bool hit = (dr6 & (1ULL << i)) != 0;
                // Only for instruction execution breakpoints, allow RIP match as fallback
                if (!hit && bpCond == BreakOn::Execute) {
                    hit = (pExceptionInfo->ContextRecord->Rip == reinterpret_cast<DWORD64>(bpAddr));
                }

                if (hit) {
                    if (bpCond == BreakOn::Execute) {
                        hasExecuteBp = true;
                    }
                    auto& bp = inst.m_breakpoints[i];
                    if (bp.callback) {
                        DreamCpuContext ctx{};
                        CopyFromWindowsContext(pExceptionInfo->ContextRecord, ctx);
                        DreamCrashGuard::ExecuteSafely("VehBreakpoint", [&]() {
                            bp.callback(ctx);
                        });
                        CopyToWindowsContext(ctx, pExceptionInfo->ContextRecord);
                    }
                    handled = true;
                }
            }

            if (handled) {
                // Resume Flag (RF) in EFLAGS only for Execute breakpoints (fault-type traps)
                // so CPU executes instruction without re-triggering. Do NOT set RF for data watchpoints
                // (Write / ReadWrite), as that would suppress valid execution breakpoints on the next instruction!
                if (hasExecuteBp) {
                    pExceptionInfo->ContextRecord->EFlags |= 0x10000;
                }
                pExceptionInfo->ContextRecord->Dr6 = 0;

                return EXCEPTION_CONTINUE_EXECUTION;
            }
        }

        return EXCEPTION_CONTINUE_SEARCH;
    }

public:
    static void CopyFromWindowsContext(PCONTEXT src, DreamCpuContext& dst) {
        dst.rax = src->Rax;
        dst.rbx = src->Rbx;
        dst.rcx = src->Rcx;
        dst.rdx = src->Rdx;
        dst.rsi = src->Rsi;
        dst.rdi = src->Rdi;
        dst.rbp = src->Rbp;
        dst.r8  = src->R8;
        dst.r9  = src->R9;
        dst.r10 = src->R10;
        dst.r11 = src->R11;
        dst.r12 = src->R12;
        dst.r13 = src->R13;
        dst.r14 = src->R14;
        dst.r15 = src->R15;
        dst.rflags = src->EFlags;

        const M128A* winXmm = &src->Xmm0;
        for (int i = 0; i < 16; ++i) {
            std::memcpy(dst.xmm[i].u8, &winXmm[i], 16);
            std::memset(dst.xmm[i]._pad_high, 0, 16);
        }
    }

    static void CopyToWindowsContext(const DreamCpuContext& src, PCONTEXT dst) {
        dst->Rax = src.rax;
        dst->Rbx = src.rbx;
        dst->Rcx = src.rcx;
        dst->Rdx = src.rdx;
        dst->Rsi = src.rsi;
        dst->Rdi = src.rdi;
        dst->Rbp = src.rbp;
        dst->R8  = src.r8;
        dst->R9  = src.r9;
        dst->R10 = src.r10;
        dst->R11 = src.r11;
        dst->R12 = src.r12;
        dst->R13 = src.r13;
        dst->R14 = src.r14;
        dst->R15 = src.r15;
        dst->EFlags = static_cast<DWORD>(src.rflags);

        M128A* winXmm = &dst->Xmm0;
        for (int i = 0; i < 16; ++i) {
            std::memcpy(&winXmm[i], src.xmm[i].u8, 16);
        }
    }

    void ApplyBreakpointToAllThreads(int slot, void* address, BreakOn condition, BreakSize size) {
        DWORD currentPid = GetCurrentProcessId();
        DWORD currentTid = GetCurrentThreadId();

        // 1. Current thread: apply directly via SetThreadContext
        ApplyBreakpointToThread(currentTid, slot, address, condition, size);

        // 2. Broadcast to all other worker threads via fast process-only NtGetNextThread
        constexpr ACCESS_MASK access = THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_SET_CONTEXT | THREAD_QUERY_INFORMATION;
        HMODULE hNtdll = GetModuleHandleW(L"ntdll.dll");
        pfnNtGetNextThread fnNtGetNextThread = hNtdll ?
            reinterpret_cast<pfnNtGetNextThread>(GetProcAddress(hNtdll, "NtGetNextThread")) : nullptr;

        if (fnNtGetNextThread) {
            HANDLE hThread = nullptr;
            HANDLE hNext = nullptr;
            while (fnNtGetNextThread(GetCurrentProcess(), hThread, access, 0, 0, &hNext) >= 0) {
                if (hThread) CloseHandle(hThread);
                hThread = hNext;
                DWORD tid = GetThreadId(hThread);
                if (tid != currentTid && tid != 0) {
                    ApplyBreakpointToThreadHandle(hThread, slot, address, condition, size);
                }
            }
            if (hThread) CloseHandle(hThread);
            return;
        }

        // Fallback: Toolhelp32 snapshot
        HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
        if (hSnap == INVALID_HANDLE_VALUE) return;

        THREADENTRY32 te;
        te.dwSize = sizeof(THREADENTRY32);

        if (Thread32First(hSnap, &te)) {
            do {
                if (te.th32OwnerProcessID == currentPid && te.th32ThreadID != currentTid) {
                    ApplyBreakpointToThread(te.th32ThreadID, slot, address, condition, size);
                }
            } while (Thread32Next(hSnap, &te));
        }
        CloseHandle(hSnap);
    }

    void ApplyBreakpointToThreadHandle(HANDLE hThread, int slot, void* address, BreakOn condition, BreakSize size) {
        if (!hThread) return;
        SuspendThread(hThread);

        CONTEXT ctx{};
        ctx.ContextFlags = CONTEXT_DEBUG_REGISTERS;
        if (GetThreadContext(hThread, &ctx)) {
            DWORD64 addrVal = reinterpret_cast<DWORD64>(address);
            switch (slot) {
                case 0: ctx.Dr0 = addrVal; break;
                case 1: ctx.Dr1 = addrVal; break;
                case 2: ctx.Dr2 = addrVal; break;
                case 3: ctx.Dr3 = addrVal; break;
            }

            ctx.Dr7 |= (1ULL << (slot * 2));
            int shift = 16 + (slot * 4);
            ctx.Dr7 &= ~(0xFULL << shift);

            DWORD64 condBits = static_cast<DWORD64>(condition) & 0x3;
            DWORD64 sizeBits = static_cast<DWORD64>(size) & 0x3;
            ctx.Dr7 |= ((condBits | (sizeBits << 2)) << shift);

            SetThreadContext(hThread, &ctx);
        }

        ResumeThread(hThread);
    }

    void ApplyBreakpointToThread(DWORD tid, int slot, void* address, BreakOn condition, BreakSize size) {
        bool isCurrentThread = (tid == GetCurrentThreadId());
        HANDLE hThread = isCurrentThread ? GetCurrentThread() : OpenThread(THREAD_GET_CONTEXT | THREAD_SET_CONTEXT | THREAD_SUSPEND_RESUME, FALSE, tid);
        if (!hThread) return;

        if (!isCurrentThread) {
            SuspendThread(hThread);
        }

        CONTEXT ctx{};
        ctx.ContextFlags = CONTEXT_DEBUG_REGISTERS;
        if (GetThreadContext(hThread, &ctx)) {
            DWORD64 addrVal = reinterpret_cast<DWORD64>(address);
            switch (slot) {
                case 0: ctx.Dr0 = addrVal; break;
                case 1: ctx.Dr1 = addrVal; break;
                case 2: ctx.Dr2 = addrVal; break;
                case 3: ctx.Dr3 = addrVal; break;
            }

            ctx.Dr7 |= (1ULL << (slot * 2));
            int shift = 16 + (slot * 4);
            ctx.Dr7 &= ~(0xFULL << shift);

            DWORD64 condBits = static_cast<DWORD64>(condition) & 0x3;
            DWORD64 sizeBits = static_cast<DWORD64>(size) & 0x3;
            ctx.Dr7 |= ((condBits | (sizeBits << 2)) << shift);

            SetThreadContext(hThread, &ctx);
        }

        if (!isCurrentThread) {
            ResumeThread(hThread);
            CloseHandle(hThread);
        }
    }

    void ClearBreakpointOnAllThreads(int slot) {
        DWORD currentPid = GetCurrentProcessId();
        DWORD currentTid = GetCurrentThreadId();

        // 1. Current thread
        ClearBreakpointOnThread(currentTid, slot);

        // 2. Broadcast to all other worker threads via fast process-only NtGetNextThread
        constexpr ACCESS_MASK access = THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_SET_CONTEXT | THREAD_QUERY_INFORMATION;
        HMODULE hNtdll = GetModuleHandleW(L"ntdll.dll");
        pfnNtGetNextThread fnNtGetNextThread = hNtdll ?
            reinterpret_cast<pfnNtGetNextThread>(GetProcAddress(hNtdll, "NtGetNextThread")) : nullptr;

        if (fnNtGetNextThread) {
            HANDLE hThread = nullptr;
            HANDLE hNext = nullptr;
            while (fnNtGetNextThread(GetCurrentProcess(), hThread, access, 0, 0, &hNext) >= 0) {
                if (hThread) CloseHandle(hThread);
                hThread = hNext;
                DWORD tid = GetThreadId(hThread);
                if (tid != currentTid && tid != 0) {
                    ClearBreakpointOnThreadHandle(hThread, slot);
                }
            }
            if (hThread) CloseHandle(hThread);
            return;
        }

        // Fallback: Toolhelp32 snapshot
        HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
        if (hSnap == INVALID_HANDLE_VALUE) return;

        THREADENTRY32 te;
        te.dwSize = sizeof(THREADENTRY32);

        if (Thread32First(hSnap, &te)) {
            do {
                if (te.th32OwnerProcessID == currentPid && te.th32ThreadID != currentTid) {
                    ClearBreakpointOnThread(te.th32ThreadID, slot);
                }
            } while (Thread32Next(hSnap, &te));
        }
        CloseHandle(hSnap);
    }

    void ClearBreakpointOnThreadHandle(HANDLE hThread, int slot) {
        if (!hThread) return;
        SuspendThread(hThread);

        CONTEXT ctx{};
        ctx.ContextFlags = CONTEXT_DEBUG_REGISTERS;
        if (GetThreadContext(hThread, &ctx)) {
            switch (slot) {
                case 0: ctx.Dr0 = 0; break;
                case 1: ctx.Dr1 = 0; break;
                case 2: ctx.Dr2 = 0; break;
                case 3: ctx.Dr3 = 0; break;
            }
            ctx.Dr7 &= ~(1ULL << (slot * 2));
            int shift = 16 + (slot * 4);
            ctx.Dr7 &= ~(0xFULL << shift);

            SetThreadContext(hThread, &ctx);
        }

        ResumeThread(hThread);
    }

    void ClearBreakpointOnThread(DWORD tid, int slot) {
        bool isCurrentThread = (tid == GetCurrentThreadId());
        HANDLE hThread = isCurrentThread ? GetCurrentThread() : OpenThread(THREAD_GET_CONTEXT | THREAD_SET_CONTEXT | THREAD_SUSPEND_RESUME, FALSE, tid);
        if (!hThread) return;

        if (!isCurrentThread) {
            SuspendThread(hThread);
        }

        CONTEXT ctx{};
        ctx.ContextFlags = CONTEXT_DEBUG_REGISTERS;
        if (GetThreadContext(hThread, &ctx)) {
            switch (slot) {
                case 0: ctx.Dr0 = 0; break;
                case 1: ctx.Dr1 = 0; break;
                case 2: ctx.Dr2 = 0; break;
                case 3: ctx.Dr3 = 0; break;
            }
            ctx.Dr7 &= ~(1ULL << (slot * 2));
            int shift = 16 + (slot * 4);
            ctx.Dr7 &= ~(0xFULL << shift);

            SetThreadContext(hThread, &ctx);
        }

        if (!isCurrentThread) {
            ResumeThread(hThread);
            CloseHandle(hThread);
        }
    }

    void* m_vehHandle{nullptr};
    HardwareBreakpointInfo m_breakpoints[4];
    std::mutex m_mutex;
};

} // namespace dreamkurumi
