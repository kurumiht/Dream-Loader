#pragma once
#include <windows.h>
#include <tlhelp32.h>
#include <intrin.h>
#include <cstdint>
#include <vector>
#include "DreamLog.hpp"

namespace dreamkurumi {

#ifndef NTSTATUS
typedef LONG NTSTATUS;
#endif

using pfnNtGetNextThread = NTSTATUS(NTAPI*)(
    HANDLE ProcessHandle,
    HANDLE ThreadHandle,
    ACCESS_MASK DesiredAccess,
    ULONG HandleAttributes,
    ULONG Flags,
    PHANDLE NewThreadHandle
);

// Zero-Stutter Process-Only Thread Freeze
// Uses NtGetNextThread to iterate only threads in the current process (15-30 us)
// instead of scanning the entire system via CreateToolhelp32Snapshot (several ms).
class FastProcessThreadFreeze {
public:
    FastProcessThreadFreeze() {
        const DWORD currentTid = GetCurrentThreadId();
        HMODULE hNtdll = GetModuleHandleW(L"ntdll.dll");
        pfnNtGetNextThread fnNtGetNextThread = hNtdll ?
            reinterpret_cast<pfnNtGetNextThread>(GetProcAddress(hNtdll, "NtGetNextThread")) : nullptr;

        constexpr ACCESS_MASK access = THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_SET_CONTEXT | THREAD_QUERY_INFORMATION;

        if (fnNtGetNextThread) {
            HANDLE hThread = nullptr;
            HANDLE hNext   = nullptr;

            while (fnNtGetNextThread(GetCurrentProcess(), hThread, access, 0, 0, &hNext) >= 0) {
                if (hThread) CloseHandle(hThread);
                hThread = hNext;

                if (GetThreadId(hThread) != currentTid) {
                    if (SuspendThread(hThread) != static_cast<DWORD>(-1)) {
                        m_suspendedThreads.push_back(hThread);
                        hThread = nullptr; // Keep handle in vector
                    }
                }
            }
            if (hThread) CloseHandle(hThread);
        } else {
            // Fallback for environments where NtGetNextThread is unavailable
            HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
            if (snapshot != INVALID_HANDLE_VALUE) {
                THREADENTRY32 entry{};
                entry.dwSize = sizeof(THREADENTRY32);
                DWORD currentPid = GetCurrentProcessId();

                if (Thread32First(snapshot, &entry)) {
                    do {
                        if (entry.th32OwnerProcessID == currentPid && entry.th32ThreadID != currentTid) {
                            HANDLE h = OpenThread(access, FALSE, entry.th32ThreadID);
                            if (h) {
                                if (SuspendThread(h) != static_cast<DWORD>(-1)) {
                                    m_suspendedThreads.push_back(h);
                                } else {
                                    CloseHandle(h);
                                }
                            }
                        }
                    } while (Thread32Next(snapshot, &entry));
                }
                CloseHandle(snapshot);
            }
        }
    }

    // Relocate any suspended thread RIP caught inside [target, target + patchSize) to trampoline
    size_t AdjustCollidingThreads(uintptr_t target, size_t patchSize, uintptr_t trampoline) {
        size_t relocated = 0;
        for (HANDLE hThread : m_suspendedThreads) {
            CONTEXT ctx{};
            ctx.ContextFlags = CONTEXT_CONTROL;
            if (GetThreadContext(hThread, &ctx)) {
                if (ctx.Rip >= target && ctx.Rip < (target + patchSize)) {
                    ctx.Rip = trampoline + (ctx.Rip - target);
                    SetThreadContext(hThread, &ctx);
                    relocated++;
                }
            }
        }
        return relocated;
    }

    size_t GetSuspendedCount() const {
        return m_suspendedThreads.size();
    }

    ~FastProcessThreadFreeze() {
        for (HANDLE hThread : m_suspendedThreads) {
            ResumeThread(hThread);
            CloseHandle(hThread);
        }
        m_suspendedThreads.clear();
    }

    // Non-copyable, non-movable
    FastProcessThreadFreeze(const FastProcessThreadFreeze&) = delete;
    FastProcessThreadFreeze& operator=(const FastProcessThreadFreeze&) = delete;

private:
    std::vector<HANDLE> m_suspendedThreads;
};

// Lock-free atomic 5-byte patch when firstInstructionLength >= 5 and inside single cache line
inline bool TryAtomicFiveBytePatch(uint8_t* target, int32_t rel32Offset, size_t firstInstrLen) {
    if (!target) return false;

    const uintptr_t addr = reinterpret_cast<uintptr_t>(target);
    const size_t cacheLineOffset = addr & 63;

    // Must be a single instruction >= 5 bytes and the 8-byte atomic window must not cross a 64-byte cache line
    if (firstInstrLen < 5 || cacheLineOffset > (64 - 8)) {
        return false;
    }

    DWORD oldProtect = 0;
    if (!VirtualProtect(target, 8, PAGE_EXECUTE_READWRITE, &oldProtect)) {
        return false;
    }

    volatile int64_t* atomicTarget = reinterpret_cast<volatile int64_t*>(target);
    int64_t expected = *atomicTarget;

    // Build the new 8-byte word: [E9][rel32 (4 bytes)][original bytes 5..7 (3 bytes)]
    uint64_t patchBytes = 0xE9ULL | (static_cast<uint64_t>(static_cast<uint32_t>(rel32Offset)) << 8);
    uint64_t preservedUpper3Bytes = static_cast<uint64_t>(expected) & 0xFFFFFF0000000000ULL;
    int64_t desired = static_cast<int64_t>(patchBytes | preservedUpper3Bytes);

    bool success = (_InterlockedCompareExchange64(atomicTarget, desired, expected) == expected);

    VirtualProtect(target, 8, oldProtect, &oldProtect);

    if (success) {
        FlushProcessWriteBuffers();
        FlushInstructionCache(GetCurrentProcess(), target, 8);
    }
    return success;
}

} // namespace dreamkurumi

namespace dreamkurumiHT {
    using namespace dreamkurumi;
}
