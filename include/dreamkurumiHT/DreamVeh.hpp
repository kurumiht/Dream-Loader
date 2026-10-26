#pragma once
#include <windows.h>
#include <tlhelp32.h>
#include <cstdint>
#include <functional>
#include <unordered_map>
#include <mutex>
#include "DreamHook.hpp"
#include "DreamLog.hpp"

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
    void* address{nullptr};
    BreakOn condition{BreakOn::Execute};
    BreakSize size{BreakSize::Bytes1};
    int slot{-1}; // DR0..DR3 (0..3)
    std::function<void(DreamCpuContext&)> callback;
    bool active{false};
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
        if (!m_vehHandle) {
            if (!Initialize()) return -1;
        }

        std::lock_guard<std::mutex> lock(m_mutex);

        int slot = -1;
        for (int i = 0; i < 4; ++i) {
            if (!m_breakpoints[i].active) {
                slot = i;
                break;
            }
        }

        if (slot == -1) {
            DREAM_LOG_WARN("DreamVeh: All 4 hardware breakpoint slots (DR0-DR3) are in use");
            return -1;
        }

        HardwareBreakpointInfo info;
        info.address = address;
        info.condition = condition;
        info.size = size;
        info.slot = slot;
        info.callback = callback;
        info.active = true;

        m_breakpoints[slot] = info;

        // Apply breakpoint across all threads (including current thread via kernel context switch)
        ApplyBreakpointToAllThreads(slot, address, condition, size);

        DREAM_LOG_INFO("DreamVeh: Set Hardware Breakpoint in DR%d at 0x%p (Condition: %d)", slot, address, static_cast<int>(condition));
        return slot;
    }

    bool RemoveBreakpoint(int slot) {
        if (slot < 0 || slot > 3) return false;
        std::lock_guard<std::mutex> lock(m_mutex);

        if (!m_breakpoints[slot].active) return false;

        m_breakpoints[slot].active = false;
        m_breakpoints[slot].address = nullptr;

        ClearBreakpointOnAllThreads(slot);
        DREAM_LOG_INFO("DreamVeh: Cleared Hardware Breakpoint DR%d", slot);
        return true;
    }

    void ClearAllBreakpoints() {
        for (int i = 0; i < 4; ++i) {
            if (m_breakpoints[i].active) {
                m_breakpoints[i].active = false;
                ClearBreakpointOnAllThreads(i);
            }
        }
    }

    // Broadcast hardware breakpoint to all worker threads (alias for SetBreakpoint)
    int BroadcastBreakpoint(void* address, BreakOn condition, BreakSize size, std::function<void(DreamCpuContext&)> callback) {
        return SetBreakpoint(address, condition, size, callback);
    }

    // Manually synchronize active breakpoints to all currently running worker threads
    void SyncAllThreads() {
        std::lock_guard<std::mutex> lock(m_mutex);
        for (int slot = 0; slot < 4; ++slot) {
            if (m_breakpoints[slot].active) {
                ApplyBreakpointToAllThreads(slot, m_breakpoints[slot].address, m_breakpoints[slot].condition, m_breakpoints[slot].size);
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
            pExceptionInfo->ContextRecord->ContextFlags |= (CONTEXT_FULL | CONTEXT_FLOATING_POINT | CONTEXT_DEBUG_REGISTERS);
            auto& inst = DreamVeh::Instance();
            DWORD64 dr6 = pExceptionInfo->ContextRecord->Dr6;

            int triggeredSlot = -1;
            for (int i = 0; i < 4; ++i) {
                if ((dr6 & (1ULL << i)) || (pExceptionInfo->ContextRecord->Rip == reinterpret_cast<DWORD64>(inst.m_breakpoints[i].address))) {
                    if (inst.m_breakpoints[i].active) {
                        triggeredSlot = i;
                        break;
                    }
                }
            }

            if (triggeredSlot != -1) {
                auto& bp = inst.m_breakpoints[triggeredSlot];
                if (bp.callback) {
                    DreamCpuContext ctx{};
                    CopyFromWindowsContext(pExceptionInfo->ContextRecord, ctx);
                    DreamCrashGuard::ExecuteSafely("VehBreakpoint", [&]() {
                        bp.callback(ctx);
                    });
                    CopyToWindowsContext(ctx, pExceptionInfo->ContextRecord);
                }

                // Resume Flag (RF) in EFLAGS so CPU executes instruction without re-triggering
                pExceptionInfo->ContextRecord->EFlags |= 0x10000;
                pExceptionInfo->ContextRecord->Dr6 = 0;

                return EXCEPTION_CONTINUE_EXECUTION;
            }
        }

        return EXCEPTION_CONTINUE_SEARCH;
    }

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

        auto* xmmSrc = reinterpret_cast<const uint8_t*>(&src->Xmm0);
        std::memcpy(dst.xmm, xmmSrc, sizeof(dst.xmm));
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

        auto* xmmDst = reinterpret_cast<uint8_t*>(&dst->Xmm0);
        std::memcpy(xmmDst, src.xmm, sizeof(src.xmm));
    }

    void ApplyBreakpointToAllThreads(int slot, void* address, BreakOn condition, BreakSize size) {
        DWORD currentPid = GetCurrentProcessId();
        DWORD currentTid = GetCurrentThreadId();

        // 1. Current thread: apply directly via SetThreadContext
        ApplyBreakpointToThread(currentTid, slot, address, condition, size);

        // 2. Broadcast to all other worker threads
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

        // 2. Broadcast to all other worker threads
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
