#pragma once
#include <cstdint>
#include <atomic>
#include <string>

namespace dreamkurumi {

enum class EngineConnectionState : uint8_t {
    Disconnected  = 0,
    Connecting    = 1,
    FindingRoute  = 2,
    Connected     = 3,
    Disconnecting = 4
};

// ============================================================================
// DreamNetStateTracker: Real-Time Network State & Ping Tracker
//
// Hooks into SteamNetworkingSockets connection life cycle (v1.3/v1.4 GNS)
// Provides deterministic connection state, latency, and packet loss metrics
// without relying on packet polling timeouts or fragile game engine offsets.
// ============================================================================
class DreamNetStateTracker {
    std::atomic<uint32_t>              m_activeConn{0};
    std::atomic<EngineConnectionState> m_state{EngineConnectionState::Disconnected};
    std::atomic<int32_t>               m_pingMs{-1};
    std::atomic<float>                 m_packetLossPct{0.0f};
    std::atomic<uint64_t>              m_lastPacketTimeMs{0};

public:
    static DreamNetStateTracker& Instance() noexcept {
        static DreamNetStateTracker s_inst;
        return s_inst;
    }

    void Reset() noexcept {
        m_activeConn.store(0, std::memory_order_relaxed);
        m_state.store(EngineConnectionState::Disconnected, std::memory_order_release);
        m_pingMs.store(-1, std::memory_order_relaxed);
        m_packetLossPct.store(0.0f, std::memory_order_relaxed);
        m_lastPacketTimeMs.store(0, std::memory_order_relaxed);
    }

    // Called from Connect / OnSteamNetConnectionStatusChanged callback or GNS message flow
    void OnConnectionStateChanged(uint32_t hConn, int32_t steamState) noexcept {
        if (steamState == 1 || steamState == 2) {
            m_activeConn.store(hConn, std::memory_order_relaxed);
            m_state.store(EngineConnectionState::Connecting, std::memory_order_release);
        } else if (steamState == 3) { // k_ESteamNetworkingConnectionState_Connected
            m_activeConn.store(hConn, std::memory_order_relaxed);
            m_state.store(EngineConnectionState::Connected, std::memory_order_release);
        } else { // 0 (None), 4 (ClosedByPeer), 5 (ProblemDetectedLocally), -1 (Dead)
            if (m_activeConn.load(std::memory_order_relaxed) == hConn || m_activeConn.load(std::memory_order_relaxed) == 0 || hConn == 0) {
                m_activeConn.store(0, std::memory_order_relaxed);
                m_state.store(EngineConnectionState::Disconnected, std::memory_order_release);
                m_pingMs.store(-1, std::memory_order_relaxed);
            }
        }
    }

    void RecordPacketActivity(uint32_t hConn = 0) noexcept {
        m_lastPacketTimeMs.store(GetTickCount64(), std::memory_order_relaxed);
        if (hConn != 0 && m_activeConn.load(std::memory_order_relaxed) == 0) {
            m_activeConn.store(hConn, std::memory_order_relaxed);
        }
        if (m_state.load(std::memory_order_relaxed) != EngineConnectionState::Connected) {
            m_state.store(EngineConnectionState::Connected, std::memory_order_release);
        }
    }

    void UpdateRealTimeMetrics(int32_t pingMs, float packetLossPct) noexcept {
        m_pingMs.store(pingMs, std::memory_order_relaxed);
        m_packetLossPct.store(packetLossPct, std::memory_order_relaxed);
    }

    EngineConnectionState GetState() const noexcept {
        auto st = m_state.load(std::memory_order_acquire);
        if (st == EngineConnectionState::Connected) {
            uint64_t now = GetTickCount64();
            uint64_t last = m_lastPacketTimeMs.load(std::memory_order_relaxed);
            if (last != 0 && now >= last && (now - last) > 10000) {
                return EngineConnectionState::Disconnected;
            }
        }
        return st;
    }

    bool IsConnected() const noexcept {
        return GetState() == EngineConnectionState::Connected;
    }

    int32_t GetPingMs() const noexcept {
        return m_pingMs.load(std::memory_order_relaxed);
    }

    float GetPacketLossPct() const noexcept {
        return m_packetLossPct.load(std::memory_order_relaxed);
    }

    uint32_t GetActiveConnectionHandle() const noexcept {
        return m_activeConn.load(std::memory_order_relaxed);
    }

    const char* GetStateString() const noexcept {
        switch (GetState()) {
            case EngineConnectionState::Connecting:
            case EngineConnectionState::FindingRoute:  return "CONNECTING";
            case EngineConnectionState::Connected:     return "CONNECTED";
            case EngineConnectionState::Disconnecting: return "DISCONNECTING";
            default:                                   return "DISCONNECTED";
        }
    }
};

} // namespace dreamkurumi
