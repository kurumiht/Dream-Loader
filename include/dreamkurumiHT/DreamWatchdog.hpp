#pragma once
#include <windows.h>
#include <cstdint>
#include <string>
#include <vector>
#include <unordered_map>
#include <chrono>
#include <thread>
#include <atomic>
#include <mutex>
#include <memory>
#include <functional>
#include <sstream>
#include <iomanip>
#include <tlhelp32.h>
#include <psapi.h>
#include "DreamLog.hpp"
#include "DreamCrashDump.hpp"

namespace dreamkurumi {

// ============================================================================
// Hang Diagnostics Report Structure
// ============================================================================
struct HangReport {
    DWORD threadId{0};
    std::string threadName{"WorkerThread"};
    uint64_t hangDurationMs{0};
    uintptr_t rip{0};
    uintptr_t rsp{0};
    uintptr_t rbp{0};
    std::string faultingModule{"Unknown"};
    uintptr_t faultingRva{0};
    std::vector<CrashFrame> callstack;
    std::string rawReport;
};

// ============================================================================
// DreamWatchdog - Anti-Hang & Thread Deadlock Diagnostic Guard
// Monitors game worker threads and heartbeat signals.
// Intercepts unresponsive threads, captures callstack & registers, and saves
// 'dream_hang_report_*.txt' without crashing the game!
// ============================================================================
class DreamWatchdog {
public:
    using HangCallback = std::function<void(const HangReport&)>;

    struct WatchedThread {
        DWORD threadId{0};
        std::string threadName;
        std::atomic<uint64_t> lastHeartbeatMs{0};
        std::atomic<bool> isHung{false};
        std::atomic<uint64_t> totalHeartbeats{0};

        WatchedThread() = default;
        WatchedThread(DWORD id, const std::string& name, uint64_t hb)
            : threadId(id), threadName(name), lastHeartbeatMs(hb), isHung(false), totalHeartbeats(1) {}
    };

    static DreamWatchdog& Instance() {
        static DreamWatchdog s_instance;
        return s_instance;
    }

    // Proactively audit RAM, process memory, and GPU capture process proliferation
    void CheckSystemHealth() {
        MEMORYSTATUSEX memStatus{};
        memStatus.dwLength = sizeof(memStatus);
        if (GlobalMemoryStatusEx(&memStatus)) {
            uint64_t totalPhysMb = memStatus.ullTotalPhys / (1024 * 1024);
            uint64_t availPhysMb = memStatus.ullAvailPhys / (1024 * 1024);
            DREAM_LOG_INFO("DreamWatchdog: Physical Memory Status - Total: %llu MB, Free: %llu MB (Memory Load: %u%%)",
                           totalPhysMb, availPhysMb, memStatus.dwMemoryLoad);

            if (availPhysMb < 2048) {
                DREAM_LOG_WARN("DreamWatchdog: [CRITICAL LOW MEMORY] Available RAM is under 2GB (%llu MB)! Out-of-memory crash risk is high.", availPhysMb);
            }
        }

        PROCESS_MEMORY_COUNTERS_EX pmc{};
        if (GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&pmc), sizeof(pmc))) {
            uint64_t workingSetMb = pmc.WorkingSetSize / (1024 * 1024);
            uint64_t privateCommitMb = pmc.PrivateUsage / (1024 * 1024);
            DREAM_LOG_INFO("DreamWatchdog: Process Memory - WorkingSet: %llu MB, PrivateCommit: %llu MB",
                           workingSetMb, privateCommitMb);
            if (workingSetMb > 12288) { // > 12 GB
                DREAM_LOG_WARN("DreamWatchdog: [HIGH MEMORY USAGE] Process WorkingSet exceeds 12GB (%llu MB)!", workingSetMb);
            }
        }

        HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (hSnap != INVALID_HANDLE_VALUE) {
            PROCESSENTRY32W pe{};
            pe.dwSize = sizeof(pe);
            int captureCount = 0;
            if (Process32FirstW(hSnap, &pe)) {
                do {
                    if (_wcsicmp(pe.szExeFile, L"SteelSeriesCaptureSvc.exe") == 0) {
                        captureCount++;
                    }
                } while (Process32NextW(hSnap, &pe));
            }
            CloseHandle(hSnap);

            if (captureCount > 2) {
                DREAM_LOG_WARN("DreamWatchdog: [HIGH RISK] Detected %d instances of 'SteelSeriesCaptureSvc.exe' running! "
                               "Orphaned capture services consume massive VRAM and cause Vulkan 'VK_ERROR_OUT_OF_DEVICE_MEMORY' crashes. "
                               "Please restart SteelSeries GG or close orphaned processes.", captureCount);
            }
        }
    }

    // Register a thread to be monitored by the watchdog
    void RegisterThread(DWORD threadId = 0, const std::string& name = "") {
        if (threadId == 0) threadId = GetCurrentThreadId();
        std::lock_guard<std::mutex> lock(m_mutex);

        std::string tName = name.empty() ? ("Thread-" + std::to_string(threadId)) : name;
        auto wt = std::make_shared<WatchedThread>(threadId, tName, GetTickCount64());
        m_threads[threadId] = wt;
        if (threadId == GetCurrentThreadId()) {
            t_cachedWatchedThread = wt;
            t_cachedThreadId = threadId;
        }

        DREAM_LOG_INFO("DreamWatchdog: Registered monitored thread '%s' (TID: %lu)",
                       tName.c_str(), threadId);
    }

    // Fast thread-local cached shared_ptr to guarantee stable object memory address across hash map rehashes
    inline static thread_local std::shared_ptr<WatchedThread> t_cachedWatchedThread = nullptr;
    inline static thread_local DWORD t_cachedThreadId = 0;

    // Monitored thread calls this regularly to prove it is alive and responsive (Lock-Free Hot Path)
    void Heartbeat(DWORD threadId = 0) {
        if (threadId == 0) threadId = GetCurrentThreadId();
        uint64_t nowMs = GetTickCount64();

        // Lock-free fast path for current thread (Address stable via shared_ptr)
        if (t_cachedWatchedThread && t_cachedThreadId == threadId) {
            t_cachedWatchedThread->lastHeartbeatMs.store(nowMs, std::memory_order_relaxed);
            t_cachedWatchedThread->isHung.store(false, std::memory_order_relaxed);
            t_cachedWatchedThread->totalHeartbeats.fetch_add(1, std::memory_order_relaxed);
            return;
        }

        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_threads.find(threadId);
        if (it != m_threads.end()) {
            it->second->lastHeartbeatMs.store(nowMs, std::memory_order_relaxed);
            it->second->isHung.store(false, std::memory_order_relaxed);
            it->second->totalHeartbeats.fetch_add(1, std::memory_order_relaxed);
            if (threadId == GetCurrentThreadId()) {
                t_cachedWatchedThread = it->second;
                t_cachedThreadId = threadId;
            }
        } else {
            // Auto-register on first heartbeat if not registered
            std::string tName = "Thread-" + std::to_string(threadId);
            auto wt = std::make_shared<WatchedThread>(threadId, tName, nowMs);
            m_threads[threadId] = wt;
            if (threadId == GetCurrentThreadId()) {
                t_cachedWatchedThread = wt;
                t_cachedThreadId = threadId;
            }
        }
    }

    // Stop monitoring a thread (e.g. upon normal exit)
    void UnregisterThread(DWORD threadId = 0) {
        if (threadId == 0) threadId = GetCurrentThreadId();
        if (t_cachedThreadId == threadId) {
            t_cachedWatchedThread.reset();
            t_cachedThreadId = 0;
        }
        std::lock_guard<std::mutex> lock(m_mutex);
        m_threads.erase(threadId);
        DREAM_LOG_INFO("DreamWatchdog: Unregistered thread (TID: %lu)", threadId);
    }

    // Start background watchdog monitor
    void Start(uint32_t checkIntervalMs = 500, uint32_t timeoutMs = 5000) {
        if (m_isRunning.load()) return;
        m_checkIntervalMs = checkIntervalMs;
        m_timeoutMs = timeoutMs;
        m_isRunning.store(true);

        m_monitorThread = std::thread([this]() {
            uint32_t healthCounter = 0;
            uint32_t healthIntervalTicks = (m_checkIntervalMs > 0) ? (60000 / m_checkIntervalMs) : 120;
            while (m_isRunning.load()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(m_checkIntervalMs));
                if (!m_isRunning.load()) break;
                CheckHangs();
                if (++healthCounter >= healthIntervalTicks) {
                    healthCounter = 0;
                    CheckSystemHealth();
                }
            }
        });

        DREAM_LOG_INFO("DreamWatchdog: Background monitor started (Interval: %u ms, Hang Timeout: %u ms)",
                       checkIntervalMs, timeoutMs);
    }

    // Stop background watchdog monitor
    void Stop() {
        if (!m_isRunning.load()) return;
        m_isRunning.store(false);
        if (m_monitorThread.joinable()) {
            m_monitorThread.join();
        }
        DREAM_LOG_INFO("DreamWatchdog: Background monitor stopped");
    }

    // Set custom hang callback
    void SetHangCallback(HangCallback cb) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_hangCallback = cb;
    }

    void SetTimeout(uint32_t timeoutMs) {
        m_timeoutMs = timeoutMs;
    }

    uint32_t GetTimeout() const {
        return m_timeoutMs;
    }

    size_t GetMonitoredCount() {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_threads.size();
    }

    bool IsThreadHung(DWORD threadId) {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_threads.find(threadId);
        return (it != m_threads.end()) ? it->second->isHung.load(std::memory_order_relaxed) : false;
    }

    // Check for hung threads and generate diagnostic reports
    std::vector<HangReport> CheckHangs() {
        uint64_t nowMs = GetTickCount64();
        std::vector<std::shared_ptr<WatchedThread>> hungCandidates;
        std::vector<uint64_t> durations;

        {
            std::lock_guard<std::mutex> lock(m_mutex);
            for (auto& [tid, wt] : m_threads) {
                uint64_t lastHb = wt->lastHeartbeatMs.load(std::memory_order_relaxed);
                uint64_t elapsed = (nowMs >= lastHb) ? (nowMs - lastHb) : 0;
                if (elapsed >= m_timeoutMs) {
                    if (!wt->isHung.load(std::memory_order_relaxed)) {
                        wt->isHung.store(true, std::memory_order_relaxed);
                        hungCandidates.push_back(wt);
                        durations.push_back(elapsed);
                    }
                }
            }
        }

        std::vector<HangReport> reports;
        for (size_t i = 0; i < hungCandidates.size(); ++i) {
            auto report = DiagnoseHang(*hungCandidates[i], durations[i]);
            reports.push_back(report);

            if (m_hangCallback) {
                DreamCrashGuard::ExecuteSafely("WatchdogHangCallback", [&]() {
                    m_hangCallback(report);
                });
            }
        }

        return reports;
    }

    // Diagnostic routine: temporarily samples the hung thread state
    HangReport DiagnoseHang(const WatchedThread& wt, uint64_t durationMs) {
        HangReport hr;
        hr.threadId = wt.threadId;
        hr.threadName = wt.threadName;
        hr.hangDurationMs = durationMs;

        if (wt.threadId == GetCurrentThreadId()) {
            CONTEXT ctx{};
            ctx.ContextFlags = CONTEXT_CONTROL | CONTEXT_INTEGER;
            RtlCaptureContext(&ctx);
            hr.rip = ctx.Rip;
            hr.rsp = ctx.Rsp;
            hr.rbp = ctx.Rbp;
            DreamCrashDump::ResolveModuleAndRva(hr.rip, hr.faultingModule, hr.faultingRva);
        } else {
            HANDLE hThread = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION,
                                        FALSE, wt.threadId);
            if (hThread) {
                DWORD prevCount = SuspendThread(hThread);
                if (prevCount != static_cast<DWORD>(-1)) {
                    CONTEXT ctx{};
                    ctx.ContextFlags = CONTEXT_CONTROL | CONTEXT_INTEGER;
                    if (GetThreadContext(hThread, &ctx)) {
                        hr.rip = ctx.Rip;
                        hr.rsp = ctx.Rsp;
                        hr.rbp = ctx.Rbp;

                        // Resolve faulting location
                        DreamCrashDump::ResolveModuleAndRva(hr.rip, hr.faultingModule, hr.faultingRva);
                    }
                    ResumeThread(hThread);
                }
                CloseHandle(hThread);
            } else {
                hr.faultingModule = "UnaccessibleThread";
            }
        }

        // Format detailed hang report
        hr.rawReport = FormatHangReport(hr);
        std::string filename = GenerateHangFileName();
        WriteHangFile(filename, hr.rawReport);

        DREAM_LOG_ERROR("DreamWatchdog: [DEADLOCK / HANG DETECTED] Monitored thread '%s' (TID: %lu) hung for %llu ms! Diagnostic report saved to '%s'",
                        hr.threadName.c_str(), hr.threadId, static_cast<unsigned long long>(hr.hangDurationMs), filename.c_str());

        return hr;
    }

private:
    DreamWatchdog() = default;
    ~DreamWatchdog() { Stop(); }

    static std::string GenerateHangFileName() {
        auto now = std::chrono::system_clock::now();
        std::time_t t = std::chrono::system_clock::to_time_t(now);
        std::tm tm;
        localtime_s(&tm, &t);
        char buf[64];
        snprintf(buf, sizeof(buf), "dream_hang_report_%04d%02d%02d_%02d%02d%02d.txt",
                 tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);
        return std::string(buf);
    }

    static bool WriteHangFile(const std::string& filename, const std::string& content) {
        FILE* fp = fopen(filename.c_str(), "w");
        if (!fp) return false;
        fputs(content.c_str(), fp);
        fclose(fp);
        return true;
    }

    static std::string FormatHangReport(const HangReport& hr) {
        std::stringstream ss;
        ss << "================================================================================\n";
        ss << "                    DREAMKURUMI HT THREAD HANG & DEADLOCK REPORT                \n";
        ss << "================================================================================\n\n";
        ss << "Thread Name          : " << hr.threadName << "\n";
        ss << "Thread ID (TID)      : " << hr.threadId << "\n";
        ss << "Hang Duration        : " << hr.hangDurationMs << " ms\n";
        ss << "Frozen Instruction   : 0x" << std::hex << std::uppercase << hr.rip << "\n";
        ss << "Frozen Module + RVA  : " << hr.faultingModule << " + 0x" << hr.faultingRva << "\n";
        ss << "Stack Pointer (RSP)  : 0x" << hr.rsp << "\n";
        ss << "Base Pointer  (RBP)  : 0x" << hr.rbp << "\n\n";

        ss << "--------------------------------------------------------------------------------\n";
        ss << " DIAGNOSIS SUMMARY\n";
        ss << "--------------------------------------------------------------------------------\n";
        ss << "  The monitored thread failed to issue a heartbeat within the configured threshold.\n";
        ss << "  Potential Causes:\n";
        ss << "    1. Infinite loop without exit condition (e.g. while(true) {} in mod code)\n";
        ss << "    2. Thread Deadlock (waiting on a mutex/critical section held by another thread)\n";
        ss << "    3. Synchronous blocking socket/file I/O on the main render or task fiber thread\n";
        ss << "\n================================================================================\n";
        ss << " End of hang report. dreamkurumiHT framework diagnostics.\n";
        ss << "================================================================================\n";
        return ss.str();
    }

    std::atomic<bool> m_isRunning{false};
    uint32_t m_checkIntervalMs{500};
    uint32_t m_timeoutMs{5000};
    std::thread m_monitorThread;
    std::unordered_map<DWORD, std::shared_ptr<WatchedThread>> m_threads;
    HangCallback m_hangCallback;
    std::mutex m_mutex;
};

} // namespace dreamkurumi
