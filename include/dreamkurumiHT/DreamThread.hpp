#pragma once
#include <windows.h>
#include <tlhelp32.h>
#include <cstdint>
#include <cstddef>
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

// ============================================================================
// Instruction Pointer Map Entry for Non-Linear Trampoline Relocations
// ============================================================================
struct DreamIpMap {
    uint8_t orig;
    uint8_t tramp;
};

// ============================================================================
// Scoped Thread Freeze with Safe RIP Relocation (Anti-Crash during Hook writes)
// Suspends worker threads using fast NtGetNextThread (15-30 us, zero system scan),
// forces synchronous stoppage via GetThreadContext, performs ZERO heap allocations
// while frozen, and maps RIP through DreamIpMap.
// ============================================================================
class ScopedThreadFreeze {
public:
    ScopedThreadFreeze(void* patchStart = nullptr, size_t patchSize = 0, void* trampolineStart = nullptr,
                       const DreamIpMap* ipMap = nullptr, size_t ipMapCount = 0)
        : m_patchStart(reinterpret_cast<uintptr_t>(patchStart)),
          m_patchSize(patchSize),
          m_trampolineStart(reinterpret_cast<uintptr_t>(trampolineStart)),
          m_ipMap(ipMap),
          m_ipMapCount(ipMapCount)
    {
        FreezeAll();
    }

    ~ScopedThreadFreeze() {
        ResumeAll();
    }

    // Non-copyable, non-movable
    ScopedThreadFreeze(const ScopedThreadFreeze&) = delete;
    ScopedThreadFreeze& operator=(const ScopedThreadFreeze&) = delete;

    size_t GetSuspendedCount() const { return m_suspendedCount; }
    size_t GetRelocatedCount() const { return m_relocatedThreads; }

private:
    bool TranslateIp(uintptr_t rip, uintptr_t& out) const {
        if (!m_patchStart || m_patchSize == 0 || !m_trampolineStart) return false;
        if (rip < m_patchStart || rip >= (m_patchStart + m_patchSize)) return false;

        uintptr_t off = rip - m_patchStart;
        if (m_ipMap && m_ipMapCount > 0) {
            for (size_t i = 0; i < m_ipMapCount; ++i) {
                if (m_ipMap[i].orig == off) {
                    out = m_trampolineStart + m_ipMap[i].tramp;
                    return true;
                }
            }
        }
        // Fallback linear mapping
        out = m_trampolineStart + off;
        return true;
    }

    void FreezeAll() {
        DWORD currentThreadId = GetCurrentThreadId();
        DWORD currentProcessId = GetCurrentProcessId();

        constexpr ACCESS_MASK access = THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_SET_CONTEXT | THREAD_QUERY_INFORMATION;

        HMODULE hNtdll = GetModuleHandleW(L"ntdll.dll");
        pfnNtGetNextThread fnNtGetNextThread = hNtdll ?
            reinterpret_cast<pfnNtGetNextThread>(GetProcAddress(hNtdll, "NtGetNextThread")) : nullptr;

        if (fnNtGetNextThread) {
            HANDLE hThread = nullptr;
            HANDLE hNext   = nullptr;

            while (fnNtGetNextThread(GetCurrentProcess(), hThread, access, 0, 0, &hNext) >= 0) {
                if (hThread) CloseHandle(hThread);
                hThread = hNext;

                if (GetThreadId(hThread) != currentThreadId) {
                    if (m_suspendedCount < 128) {
                        if (SuspendThread(hThread) != static_cast<DWORD>(-1)) {
                            m_suspendedThreads[m_suspendedCount++] = hThread;

                            CONTEXT ctx{};
                            ctx.ContextFlags = CONTEXT_CONTROL;
                            if (GetThreadContext(hThread, &ctx)) {
                                uintptr_t newRip = 0;
                                if (TranslateIp(ctx.Rip, newRip)) {
                                    ctx.Rip = newRip;
                                    SetThreadContext(hThread, &ctx);
                                    m_relocatedThreads++;
                                }
                            }
                            hThread = nullptr; // Handed off to m_suspendedThreads
                            continue;
                        }
                    }
                }
            }
            if (hThread) CloseHandle(hThread);
            return;
        }

        // Fallback: Toolhelp32 snapshot
        HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
        if (snapshot == INVALID_HANDLE_VALUE) return;

        THREADENTRY32 entry;
        entry.dwSize = sizeof(THREADENTRY32);

        if (Thread32First(snapshot, &entry)) {
            do {
                if (entry.th32OwnerProcessID == currentProcessId && entry.th32ThreadID != currentThreadId) {
                    if (m_suspendedCount >= 128) break;

                    HANDLE hThread = OpenThread(access, FALSE, entry.th32ThreadID);
                    if (hThread) {
                        if (SuspendThread(hThread) != static_cast<DWORD>(-1)) {
                            m_suspendedThreads[m_suspendedCount++] = hThread;

                            CONTEXT ctx{};
                            ctx.ContextFlags = CONTEXT_CONTROL;
                            if (GetThreadContext(hThread, &ctx)) {
                                uintptr_t newRip = 0;
                                if (TranslateIp(ctx.Rip, newRip)) {
                                    ctx.Rip = newRip;
                                    SetThreadContext(hThread, &ctx);
                                    m_relocatedThreads++;
                                }
                            }
                        } else {
                            CloseHandle(hThread);
                        }
                    }
                }
            } while (Thread32Next(snapshot, &entry));
        }

        CloseHandle(snapshot);
    }

    void ResumeAll() {
        for (size_t i = m_suspendedCount; i > 0; --i) {
            HANDLE hThread = m_suspendedThreads[i - 1];
            ResumeThread(hThread);
            CloseHandle(hThread);
        }
        size_t relocated = m_relocatedThreads;
        m_suspendedCount = 0;

        // Now safe to log after all threads have resumed
        if (relocated > 0) {
            DREAM_LOG_WARN("DreamThread: Relocated %zu worker thread(s) from patch bytes to trampoline", relocated);
        }
    }

    uintptr_t m_patchStart{0};
    size_t m_patchSize{0};
    uintptr_t m_trampolineStart{0};
    const DreamIpMap* m_ipMap{nullptr};
    size_t m_ipMapCount{0};

    // Preallocated fixed buffer: ZERO malloc/HeapAlloc while threads are suspended!
    HANDLE m_suspendedThreads[128]{nullptr};
    size_t m_suspendedCount{0};
    size_t m_relocatedThreads{0};
};

} // namespace dreamkurumi
