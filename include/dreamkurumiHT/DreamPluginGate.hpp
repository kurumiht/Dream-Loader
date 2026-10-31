#pragma once
#include <windows.h>
#include <atomic>
#include <chrono>
#include <thread>
#include <string>
#include <unordered_map>
#include <mutex>
#include <vector>
#include "DreamLog.hpp"

namespace dreamkurumi {

// ============================================================================
// DreamPluginGate: Epilogue-Safe Execution Gate & Zero-Freeze Hot-Reload Draining
//
// Solves the Function Epilogue (RET) Race Window:
// When hot-reloading a mod DLL, the host core owns the gate. The gate is entered
// before invoking plugin code and left ONLY after execution returns to the host
// core (after the plugin function's epilogue 'ret' instruction has finished).
//
// Zero-Freeze Hot-Reload:
// Instead of suspending threads (ScopedThreadFreeze), CloseAndDrain() uses the
// Win32 FlushProcessWriteBuffers() system API to issue an Inter-Processor
// Interrupt (IPI), guaranteeing an asymmetric full memory barrier on all cores.
// ============================================================================

class PluginExecutionGate {
public:
    static constexpr uint64_t FLAG_CLOSING = 0x8000000000000000ULL;
    static constexpr uint64_t COUNT_MASK   = 0x7FFFFFFFFFFFFFFFULL;

    PluginExecutionGate() : m_state(0) {}

    // Attempts to enter the plugin execution scope.
    // Returns true if successfully entered; false if the plugin is closing/unloading.
    [[nodiscard]] __forceinline bool TryEnter() noexcept {
        uint64_t cur = m_state.load(std::memory_order_relaxed);
        for (;;) {
            if (cur & FLAG_CLOSING) [[unlikely]] {
                return false;
            }
            if (m_state.compare_exchange_weak(cur, cur + 1, std::memory_order_acquire, std::memory_order_relaxed)) {
                return true;
            }
        }
    }

    // Leaves the execution scope. MUST be called by the host engine after the
    // plugin call returns to host code (after RET instruction has executed).
    __forceinline void Leave() noexcept {
        uint64_t cur = m_state.load(std::memory_order_relaxed);
        for (;;) {
            if ((cur & COUNT_MASK) == 0) [[unlikely]] {
                return; // Underflow safety: prevent decrement if in-flight count is already 0
            }
            if (m_state.compare_exchange_weak(cur, cur - 1, std::memory_order_release, std::memory_order_relaxed)) {
                return;
            }
        }
    }

    // Number of threads currently executing inside the plugin
    [[nodiscard]] __forceinline uint64_t GetInFlightCount() const noexcept {
        return m_state.load(std::memory_order_acquire) & COUNT_MASK;
    }

    // Check if the gate has been closed
    [[nodiscard]] __forceinline bool IsClosing() const noexcept {
        return (m_state.load(std::memory_order_acquire) & FLAG_CLOSING) != 0;
    }

    // Reopen gate (e.g. after successful reload)
    void Reopen() noexcept {
        m_state.store(0, std::memory_order_release);
    }

    // Closes gate to prevent new entries and drains all in-flight calls.
    // Uses FlushProcessWriteBuffers() (IPI barrier) to ensure all cores observe closing state.
    bool CloseAndDrain(uint32_t timeoutMs = 2000) noexcept {
        // Set closing flag
        m_state.fetch_or(FLAG_CLOSING, std::memory_order_release);

        // Asymmetric memory barrier: forces all CPU cores to flush write buffers
        FlushProcessWriteBuffers();

        const uint64_t startTick = GetTickCount64();
        while ((m_state.load(std::memory_order_acquire) & COUNT_MASK) > 0) {
            if (GetTickCount64() - startTick >= timeoutMs) {
                return false; // Timed out, callers still stuck inside plugin
            }
            YieldOrSleep();
        }
        return true;
    }

private:
    static void YieldOrSleep() noexcept {
        for (int i = 0; i < 32; ++i) {
#if defined(_M_X64) || defined(__x86_64__)
            _mm_pause();
#endif
        }
        SwitchToThread();
    }

    alignas(64) std::atomic<uint64_t> m_state{0};
};

// RAII Guard for executing through a PluginExecutionGate
class ScopedPluginGateLock {
public:
    explicit ScopedPluginGateLock(PluginExecutionGate& gate) noexcept
        : m_gate(gate), m_entered(gate.TryEnter()) {}

    ~ScopedPluginGateLock() noexcept {
        if (m_entered) {
            m_gate.Leave();
        }
    }

    [[nodiscard]] bool IsEntered() const noexcept { return m_entered; }
    explicit operator bool() const noexcept { return m_entered; }

private:
    PluginExecutionGate& m_gate;
    bool m_entered{false};
};

// Global Registry for Plugin Gates & Quarantine Management
class PluginGateRegistry {
public:
    static PluginGateRegistry& Instance() {
        static PluginGateRegistry s_instance;
        return s_instance;
    }

    PluginExecutionGate& GetOrCreateGate(const std::string& modName) {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_gates[modName];
    }

    bool SafeUnloadModule(const std::string& modName, HMODULE hModule, uint32_t drainTimeoutMs = 2000) {
        PluginExecutionGate* gate = nullptr;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            auto it = m_gates.find(modName);
            if (it != m_gates.end()) {
                gate = &it->second;
            }
        }

        if (gate) {
            bool drained = gate->CloseAndDrain(drainTimeoutMs);
            if (!drained) {
                DREAM_LOG_ERROR("PluginGateRegistry: Module '%s' failed to drain within %u ms! In-flight calls: %llu. Quarantining to prevent crash.",
                                modName.c_str(), drainTimeoutMs, static_cast<unsigned long long>(gate->GetInFlightCount()));
                QuarantineModule(hModule);
                return false;
            }
        }

        if (hModule) {
            FreeLibrary(hModule);
            DREAM_LOG_INFO("PluginGateRegistry: Module '%s' cleanly unmapped after zero-freeze drain.", modName.c_str());
        }
        return true;
    }

    void QuarantineModule(HMODULE hModule) {
        if (!hModule) return;
        std::lock_guard<std::mutex> lock(m_mutex);
        m_quarantinedModules.push_back(hModule);
    }

    size_t GetQuarantineCount() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_quarantinedModules.size();
    }

    void ClearQuarantine() {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_quarantinedModules.clear();
    }

private:
    PluginGateRegistry() = default;
    mutable std::mutex m_mutex;
    std::unordered_map<std::string, PluginExecutionGate> m_gates;
    std::vector<HMODULE> m_quarantinedModules;
};

} // namespace dreamkurumi
