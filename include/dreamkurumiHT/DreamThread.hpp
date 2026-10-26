#pragma once
#include <windows.h>
#include <tlhelp32.h>
#include <vector>
#include <cstdint>
#include "DreamLog.hpp"

namespace dreamkurumi {

// ============================================================================
// Scoped Thread Freeze with RIP Relocation (Anti-Crash during Hook writes)
// Suspends worker threads and relocates Instruction Pointer if paused inside patch
// ============================================================================
class ScopedThreadFreeze {
public:
    ScopedThreadFreeze(void* patchStart = nullptr, size_t patchSize = 0, void* trampolineStart = nullptr)
        : m_patchStart(reinterpret_cast<uintptr_t>(patchStart)),
          m_patchSize(patchSize),
          m_trampolineStart(reinterpret_cast<uintptr_t>(trampolineStart))
    {
        FreezeAll();
    }

    ~ScopedThreadFreeze() {
        ResumeAll();
    }

    // Non-copyable, non-movable
    ScopedThreadFreeze(const ScopedThreadFreeze&) = delete;
    ScopedThreadFreeze& operator=(const ScopedThreadFreeze&) = delete;

    size_t GetSuspendedCount() const { return m_suspendedThreads.size(); }
    size_t GetRelocatedCount() const { return m_relocatedThreads; }

private:
    void FreezeAll() {
        DWORD currentThreadId = GetCurrentThreadId();
        DWORD currentProcessId = GetCurrentProcessId();

        HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
        if (snapshot == INVALID_HANDLE_VALUE) return;

        THREADENTRY32 entry;
        entry.dwSize = sizeof(THREADENTRY32);

        if (Thread32First(snapshot, &entry)) {
            do {
                if (entry.th32OwnerProcessID == currentProcessId && entry.th32ThreadID != currentThreadId) {
                    HANDLE hThread = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_SET_CONTEXT, FALSE, entry.th32ThreadID);
                    if (hThread) {
                        if (SuspendThread(hThread) != static_cast<DWORD>(-1)) {
                            m_suspendedThreads.push_back(hThread);

                            // Inspect context if thread was interrupted inside the patch region
                            if (m_patchStart && m_patchSize > 0 && m_trampolineStart) {
                                CONTEXT ctx{};
                                ctx.ContextFlags = CONTEXT_CONTROL;
                                if (GetThreadContext(hThread, &ctx)) {
                                    if (ctx.Rip >= m_patchStart && ctx.Rip < (m_patchStart + m_patchSize)) {
                                        uintptr_t offset = ctx.Rip - m_patchStart;
                                        ctx.Rip = m_trampolineStart + offset;
                                        SetThreadContext(hThread, &ctx);
                                        m_relocatedThreads++;
                                        DREAM_LOG_WARN("DreamThread: Thread %lu IP relocated from 0x%p to trampoline 0x%p",
                                                       entry.th32ThreadID, reinterpret_cast<void*>(m_patchStart + offset), reinterpret_cast<void*>(ctx.Rip));
                                    }
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
        for (auto it = m_suspendedThreads.rbegin(); it != m_suspendedThreads.rend(); ++it) {
            ResumeThread(*it);
            CloseHandle(*it);
        }
        m_suspendedThreads.clear();
    }

    uintptr_t m_patchStart{0};
    size_t m_patchSize{0};
    uintptr_t m_trampolineStart{0};
    std::vector<HANDLE> m_suspendedThreads;
    size_t m_relocatedThreads{0};
};

} // namespace dreamkurumi
