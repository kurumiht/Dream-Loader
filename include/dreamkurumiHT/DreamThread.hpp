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
// ============================================================================
// Wine / Proton Linux Environment Detection
// Enables seamless thread suspension & watchdog timing for Headless Dedicated Servers
// running on Linux via Docker / Pterodactyl / Wine / Proton
// ============================================================================
inline bool IsRunningUnderWine() {
    static int s_isWine = -1;
    if (s_isWine != -1) return s_isWine == 1;

    HMODULE hNtdll = GetModuleHandleW(L"ntdll.dll");
    if (hNtdll) {
        typedef const char* (__cdecl *pfn_wine_get_version)(void);
        auto pfn = reinterpret_cast<pfn_wine_get_version>(GetProcAddress(hNtdll, "wine_get_version"));
        if (pfn) {
            s_isWine = 1;
            DREAM_LOG_INFO("DreamThread: Detected Wine/Proton Linux environment (Wine %s)", pfn());
            return true;
        }
    }
    s_isWine = 0;
    return false;
}

inline const char* GetWineVersion() {
    HMODULE hNtdll = GetModuleHandleW(L"ntdll.dll");
    if (hNtdll) {
        typedef const char* (__cdecl *pfn_wine_get_version)(void);
        auto pfn = reinterpret_cast<pfn_wine_get_version>(GetProcAddress(hNtdll, "wine_get_version"));
        if (pfn) return pfn();
    }
    return nullptr;
}

struct DreamIpMap {
    uint16_t orig;
    uint16_t tramp;
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

    static bool TranslateIp(uintptr_t rip, uintptr_t patchStart, size_t patchSize,
                            uintptr_t trampolineStart, const DreamIpMap* ipMap, size_t ipMapCount,
                            uintptr_t& out) {
        if (!patchStart || patchSize == 0 || !trampolineStart) return false;
        if (rip < patchStart || rip >= (patchStart + patchSize)) return false;

        uintptr_t off = rip - patchStart;
        if (ipMap && ipMapCount > 0) {
            // 1. Exact match on displaced instruction boundary
            for (size_t i = 0; i < ipMapCount; ++i) {
                if (ipMap[i].orig == off) {
                    out = trampolineStart + ipMap[i].tramp;
                    return true;
                }
            }
            // 2. Intra-instruction match (thread stopped midway within multi-byte instruction):
            for (size_t i = ipMapCount; i > 0; --i) {
                if (off >= ipMap[i - 1].orig) {
                    uintptr_t intra = off - ipMap[i - 1].orig;
                    out = trampolineStart + ipMap[i - 1].tramp + intra;
                    return true;
                }
            }
        }
        // Fallback linear mapping
        out = trampolineStart + off;
        return true;
    }

private:
    bool TranslateIp(uintptr_t rip, uintptr_t& out) const {
        return TranslateIp(rip, m_patchStart, m_patchSize, m_trampolineStart, m_ipMap, m_ipMapCount, out);
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
                    if (m_suspendedCount < MAX_FROZEN_THREADS) {
                        if (SuspendThread(hThread) != static_cast<DWORD>(-1)) {
                            HANDLE hSaved = nullptr;
                            if (DuplicateHandle(GetCurrentProcess(), hThread, GetCurrentProcess(), &hSaved, 0, FALSE, DUPLICATE_SAME_ACCESS)) {
                                m_suspendedThreads[m_suspendedCount++] = hSaved;

                                CONTEXT ctx{};
                                ctx.ContextFlags = CONTEXT_CONTROL;
                                if (GetThreadContext(hSaved, &ctx)) {
                                    uintptr_t newRip = 0;
                                    if (TranslateIp(ctx.Rip, newRip)) {
                                        ctx.Rip = newRip;
                                        SetThreadContext(hSaved, &ctx);
                                        m_relocatedThreads++;
                                    }
                                }
                            } else {
                                ResumeThread(hThread);
                            }
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
                    if (m_suspendedCount >= MAX_FROZEN_THREADS) break;

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

    static constexpr size_t MAX_FROZEN_THREADS = 1024;
    // Preallocated fixed buffer: ZERO malloc/HeapAlloc while threads are suspended!
    HANDLE m_suspendedThreads[MAX_FROZEN_THREADS]{nullptr};
    size_t m_suspendedCount{0};
    size_t m_relocatedThreads{0};
};

} // namespace dreamkurumi
