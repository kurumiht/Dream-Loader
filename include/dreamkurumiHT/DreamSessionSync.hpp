#pragma once
#include <windows.h>
#include <cstdint>
#include <atomic>
#include <string>
#include <vector>
#include <mutex>
#include <algorithm>
#include "DreamLog.hpp"
#include "DreamEnv.hpp"
#include "DreamToast.hpp"
#include "DreamNetBridge.hpp"
#include "DreamScheduler.hpp"
#include "DreamGameContext.hpp"
#include "DreamCrashGuard.hpp"
#include "DreamMemory.hpp"
#include "DreamToml.hpp"

namespace dreamkurumi {

// ============================================================================
// DreamSessionSync: Server-Client ECS Session Epoch Synchronization (v3.9.0 v1.8)
// Provides monotonic session epoch detection, heartbeat watchdog timeout (6s),
// safe world cache invalidation, dangling callback pruning, and Zero-Touch
// Auto-Resync via DreamScheduler Coroutine (Dev 1 & Dev 2 Architecture).
// ============================================================================

typedef void (*DK_SessionResetCallback)(uint64_t oldEpoch, uint64_t newEpoch, void* userData);

class DreamSessionSync {
    std::atomic<bool>     m_enabled{true};
    std::atomic<uint64_t> m_sessionEpoch{0};
    std::atomic<bool>     m_isSessionStale{false};
    std::atomic<uint64_t> m_lastHeartbeatTick{0};
    std::atomic<uint64_t> m_watchdogTimeoutMs{10000};
    uint64_t              m_serverBootTime{0};

    struct CallbackEntry {
        DK_SessionResetCallback cb{nullptr};
        void* userData{nullptr};
    };

    std::vector<CallbackEntry> m_callbacks;
    mutable std::mutex         m_mutex;

    static bool IsValidCodePointer(void* ptr) {
        if (!ptr || !MemoryHelper::IsSafeToProbeAddress(ptr, 1)) return false;
        HMODULE hNt = GetModuleHandleW(L"ntdll.dll");
        if (hNt) {
            using FnRtlPc = PVOID (NTAPI*)(PVOID, PVOID*);
            auto pfn = reinterpret_cast<FnRtlPc>(GetProcAddress(hNt, "RtlPcToFileHeader"));
            if (pfn) {
                PVOID imgBase = nullptr;
                return (pfn(ptr, &imgBase) != nullptr && imgBase != nullptr);
            }
        }
        return true;
    }

public:
    static DreamSessionSync& Instance() {
        static DreamSessionSync s_inst;
        static std::once_flag s_initOnce;
        std::call_once(s_initOnce, [&]() {
            s_inst.Initialize();
        });
        return s_inst;
    }

    void Initialize() {
        LoadConfig();
        if (DreamEnv::IsDedicatedServer()) {
            InitializeServerEpoch();
        }
        // Always register client listener so opcode 0x0010 is never unhandled
        InitializeClientListener();
    }

    void LoadConfig() {
        DreamToml toml;
        if (toml.LoadFile("mods\\dreamkurumiHT\\config.toml") ||
            toml.LoadFile("config.toml") ||
            toml.LoadFile("mods\\dreamkurumiHT\\config.ini") ||
            toml.LoadFile("config.ini")) {
            if (toml.HasSection("SessionSync")) {
                m_enabled.store(toml.GetBool("SessionSync", "EnableSessionSync", true));
                int timeoutSec = toml.GetInt("SessionSync", "WatchdogTimeoutSeconds", 10);
                if (timeoutSec < 2) timeoutSec = 2;
                if (timeoutSec > 60) timeoutSec = 60;
                m_watchdogTimeoutMs.store(static_cast<uint64_t>(timeoutSec) * 1000);
            }
        }
    }

    void InitializeServerEpoch() {
        uint64_t ticks = GetTickCount64() & 0x0000FFFFFFFFFFFFULL;
        uint64_t pid = (static_cast<uint64_t>(GetCurrentProcessId()) & 0xFFFFULL) << 48;
        uint64_t epoch = pid | ticks;
        if (epoch == 0) epoch = 1;
        m_sessionEpoch.store(epoch, std::memory_order_release);
        m_serverBootTime = GetTickCount64();
        m_lastHeartbeatTick.store(m_serverBootTime, std::memory_order_release);
        m_isSessionStale.store(false, std::memory_order_release);
        DREAM_LOG_INFO("[DreamSessionSync] Dedicated Server Session Epoch initialized: 0x%016llX (PID: %lu)",
                       (unsigned long long)epoch, GetCurrentProcessId());
    }

    void InitializeClientListener() {
        DreamNetBridge::Instance().RegisterHandler(
            DreamServerSync::SYNC_CHANNEL,
            DreamServerSync::OPCODE_SESSION_EPOCH_ANNOUNCE,
            [this](const NetPacket& pkt) {
                if (!pkt.IsFromServer() && !DreamNetBridge::Instance().IsLoopbackEnabled()) return;
                if (pkt.payload.size() < 8) return;
                NetBufferReader reader(pkt.payload);
                uint64_t incomingEpoch = reader.ReadUInt64();
                uint32_t pid = (pkt.payload.size() >= 12) ? reader.ReadUInt32() : 0;
                uint32_t uptime = (pkt.payload.size() >= 16) ? reader.ReadUInt32() : 0;
                OnReceiveServerEpoch(incomingEpoch, pid, uptime);
            }
        );
    }

    void BroadcastServerEpoch() {
        if (!DreamEnv::IsDedicatedServer()) return;
        uint64_t epoch = m_sessionEpoch.load(std::memory_order_acquire);
        if (epoch == 0) return;

        NetBufferWriter writer;
        writer.WriteUInt64(epoch);
        writer.WriteUInt32(GetCurrentProcessId());
        uint32_t uptime = static_cast<uint32_t>((GetTickCount64() - m_serverBootTime) / 1000);
        writer.WriteUInt32(uptime);

        DreamNetBridge::Instance().BroadcastToClients(
            DreamServerSync::SYNC_CHANNEL,
            DreamServerSync::OPCODE_SESSION_EPOCH_ANNOUNCE,
            writer
        );
    }

    void OnReceiveServerEpoch(uint64_t incomingEpoch, uint32_t serverPid, uint32_t uptime) {
        if (DreamEnv::IsDedicatedServer()) return;
        m_lastHeartbeatTick.store(GetTickCount64(), std::memory_order_release);

        uint64_t current = m_sessionEpoch.load(std::memory_order_acquire);
        if (current == 0) {
            // First baseline connection! (Do not trigger safe-mode on initial join)
            m_sessionEpoch.store(incomingEpoch, std::memory_order_release);
            m_isSessionStale.store(false, std::memory_order_release);
            DREAM_LOG_INFO("[DreamSessionSync] Attached to Dedicated Server Session Epoch: 0x%016llX (PID: %u, Uptime: %us)",
                           (unsigned long long)incomingEpoch, serverPid, uptime);
            return;
        }

        if (incomingEpoch != current) {
            // SERVER REBOOT DETECTED!
            m_sessionEpoch.store(incomingEpoch, std::memory_order_release);
            m_isSessionStale.store(true, std::memory_order_release);
            const uint64_t transitionTimeMs = GetTickCount64();

            DREAM_LOG_WARN("[DreamSessionSync] >>> SERVER REBOOT DETECTED! Epoch changed: 0x%016llX -> 0x%016llX (Safe-Sync Activated) <<<",
                           (unsigned long long)current, (unsigned long long)incomingEpoch);

            // Rule 3: Visual-only alert (100% visual toast, zero audio sound)
            DreamToast::Instance().Show(
                "[Server Restarted] ตรวจพบเซิร์ฟเวอร์รีสตาร์ท - กำลังรอซิงค์โลกใหม่ในโหมด Safe-Sync...",
                5.0f,
                0xFFA500 // Warning Amber
            );

            // Invalidate local player & world cache in DreamGameContext so stale pointers aren't considered ready
            DreamGameContext::Instance().SetLocalPlayer(nullptr);
            DreamGameContext::Instance().SetWorld(nullptr);

            // Notify registered plugins via C-ABI callbacks safely (prunes dead hot-reload pointers)
            InvokeResetCallbacksSafely(current, incomingEpoch);

            // Zero-Touch Auto-Resync State Machine via DreamScheduler (runs on Main Simulation Thread)
            DreamScheduler::Instance().YieldUntil(
                [this, transitionTimeMs, incomingEpoch]() -> bool {
                    // 1. If user typed /reconnect already or epoch changed again, finish task
                    if (!m_isSessionStale.load(std::memory_order_acquire) ||
                        m_sessionEpoch.load(std::memory_order_relaxed) != incomingEpoch) {
                        return true;
                    }
                    // 2. Wait at least 2500ms for world entities & packet replication to stabilize
                    if ((GetTickCount64() - transitionTimeMs) < 2500) {
                        return false;
                    }
                    // 3. Check if fresh local player has been recaptured upon world spawn
                    return dreamkurumi::DreamGameContext::Instance().IsPlayerReady();
                },
                [this, incomingEpoch]() {
                    if (m_isSessionStale.load(std::memory_order_acquire) &&
                        m_sessionEpoch.load(std::memory_order_relaxed) == incomingEpoch) {
                        InvalidateWorldCache();
                        NotifyResyncComplete();
                    }
                },
                15000,
                nullptr,
                "DreamSessionAutoResync"
            );
        }
    }

    void InvokeResetCallbacksSafely(uint64_t oldEpoch, uint64_t newEpoch) {
        std::vector<CallbackEntry> validCallbacks;
        {
            std::lock_guard<std::mutex> lk(m_mutex);
            auto it = m_callbacks.begin();
            while (it != m_callbacks.end()) {
                if (!IsValidCodePointer(reinterpret_cast<void*>(it->cb))) {
                    DREAM_LOG_WARN("[DreamSessionSync] Pruned dangling callback at %p (unloaded plugin)", (void*)it->cb);
                    it = m_callbacks.erase(it);
                } else {
                    validCallbacks.push_back(*it);
                    ++it;
                }
            }
        }

        for (const auto& entry : validCallbacks) {
            if (entry.cb) {
                DreamCrashGuard::ExecuteSafely("SessionResetCallback", [&]() {
                    entry.cb(oldEpoch, newEpoch, entry.userData);
                });
            }
        }
    }

    uint64_t GetServerSessionEpoch() const noexcept {
        return m_sessionEpoch.load(std::memory_order_acquire);
    }

    bool IsHeartbeatAlive() const noexcept {
        if (!m_enabled.load(std::memory_order_relaxed)) return true;
        if (DreamEnv::IsDedicatedServer()) return true;
        uint64_t last = m_lastHeartbeatTick.load(std::memory_order_relaxed);
        if (last == 0) return false;
        uint64_t now = GetTickCount64();
        uint64_t timeout = m_watchdogTimeoutMs.load(std::memory_order_relaxed);
        return (now >= last && (now - last) <= timeout);
    }

    bool IsSessionStale() const noexcept {
        if (!m_enabled.load(std::memory_order_relaxed)) return false;
        if (m_isSessionStale.load(std::memory_order_acquire)) return true;
        if (!DreamEnv::IsDedicatedServer()) {
            uint64_t epoch = m_sessionEpoch.load(std::memory_order_acquire);
            if (epoch != 0) {
                uint64_t last = m_lastHeartbeatTick.load(std::memory_order_relaxed);
                uint64_t timeout = m_watchdogTimeoutMs.load(std::memory_order_relaxed);
                if (last != 0 && (GetTickCount64() - last) > timeout) {
                    return true; // Heartbeat timed out
                }
            }
        }
        return false;
    }

    void InvalidateWorldCache() noexcept {
        m_isSessionStale.store(true, std::memory_order_release);
    }

    void NotifyResyncComplete() noexcept {
        m_isSessionStale.store(false, std::memory_order_release);
        DREAM_LOG_INFO("[DreamSessionSync] Session resynchronization complete. Restored full AoE pipelines.");
        DreamToast::Instance().Show("[Auto-Resync] ซิงค์ข้อมูลเซิร์ฟเวอร์สำเร็จ - คืนค่าระบบ AoE & Auto-Pickup เต็มรูปแบบอัตโนมัติ", 3.5f, 0x00FF88);
    }

    void TriggerCleanReconnect() {
        NotifyResyncComplete();
    }

    bool RegisterSessionResetCallback(DK_SessionResetCallback cb, void* userData) {
        if (!cb) return false;
        std::lock_guard<std::mutex> lk(m_mutex);
        for (const auto& entry : m_callbacks) {
            if (entry.cb == cb && entry.userData == userData) return true;
        }
        m_callbacks.push_back({cb, userData});
        return true;
    }

    bool UnregisterSessionResetCallback(DK_SessionResetCallback cb, void* userData = nullptr) {
        if (!cb) return false;
        std::lock_guard<std::mutex> lk(m_mutex);
        auto it = std::remove_if(m_callbacks.begin(), m_callbacks.end(),
            [cb, userData](const CallbackEntry& e) {
                if (userData != nullptr) {
                    return e.cb == cb && e.userData == userData;
                }
                return e.cb == cb;
            });
        if (it != m_callbacks.end()) {
            m_callbacks.erase(it, m_callbacks.end());
            return true;
        }
        return false;
    }

    size_t GetCallbackCount() const {
        std::lock_guard<std::mutex> lk(m_mutex);
        return m_callbacks.size();
    }

private:
    DreamSessionSync() = default;
};

} // namespace dreamkurumi
