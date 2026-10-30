#pragma once

// ============================================================================
// DreamRpcMultiplexer.hpp - High-Level Mod RPC Multiplexer for Enshrouded Dedicated Server
//
// Part of the dreamkurumiHT Framework (Item 8: Custom Network RPC Multiplexer)
// Features:
// - Seamless remote procedure calling between Client and Dedicated Server
// - Collision-free RPC routing via 64-bit FNV-1a procedure hashes
// - Proprietary Framing Magic Header (0xD7EA_0001) for mod data encapsulation
// - Type-safe binary argument pack/unpack using NetBufferWriter/NetBufferReader
// - Dual-mode operation: Direct Native Socket Injection & Zero-latency Local Loopback
// ============================================================================

#include <windows.h>
#include <string>
#include <vector>
#include <unordered_map>
#include <functional>
#include <mutex>
#include <atomic>
#include <chrono>
#include "DreamNetBridge.hpp"
#include "DreamHash.hpp"
#include "DreamLog.hpp"
#include "DreamCrashGuard.hpp"

namespace dreamkurumi {

constexpr uint32_t DREAM_RPC_MAGIC = 0xD7EA0001;

struct RpcInvocationContext {
    uint32_t callerId{0};       // 0 = Server, >0 = Client ID
    uint32_t targetId{0};       // 0 = Server, >0 = Client ID, 0xFFFFFFFF = All
    uint64_t rpcHash{0};
    std::string rpcName;
    NetBufferReader reader;

    RpcInvocationContext(uint32_t caller, uint32_t target, uint64_t hash, std::string name, const std::vector<uint8_t>& payload)
        : callerId(caller), targetId(target), rpcHash(hash), rpcName(std::move(name)), reader(payload) {}
};

class DreamRpcMultiplexer {
public:
    using RpcHandler = std::function<void(RpcInvocationContext& ctx)>;
    using RawPacketInterceptor = std::function<bool(const uint8_t* data, size_t size)>;

    static DreamRpcMultiplexer& Instance() {
        static DreamRpcMultiplexer s_instance;
        return s_instance;
    }

    // Register a remote procedure by name. Duplicate registrations are strictly rejected to prevent hijacking.
    bool RegisterRpc(const std::string& rpcName, RpcHandler handler) {
        uint64_t hash = DreamHash::Hash(rpcName);
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_handlers.find(hash) != m_handlers.end()) {
            DREAM_LOG_WARN("DreamRpcMultiplexer: Registration rejected: RPC '%s' (0x%016llX) already registered",
                           rpcName.c_str(), static_cast<unsigned long long>(hash));
            return false;
        }
        m_handlers[hash] = handler;
        m_nameRegistry[hash] = rpcName;
        DREAM_LOG_INFO("DreamRpcMultiplexer: Registered RPC '%s' (Hash: 0x%016llX)",
                       rpcName.c_str(), static_cast<unsigned long long>(hash));
        return true;
    }

    // Unregister an RPC
    void UnregisterRpc(const std::string& rpcName) {
        uint64_t hash = DreamHash::Hash(rpcName);
        std::lock_guard<std::mutex> lock(m_mutex);
        m_handlers.erase(hash);
        m_nameRegistry.erase(hash);
    }

    // ========================================================================
    // Client-to-Server RPC Calls
    // ========================================================================
    bool CallServer(const std::string& rpcName, const NetBufferWriter& args) {
        return DispatchRpc(GetLocalClientId(), 0, rpcName, args.GetBuffer());
    }

    bool CallServer(const std::string& rpcName) {
        NetBufferWriter empty;
        return CallServer(rpcName, empty);
    }

    // ========================================================================
    // Server-to-Client RPC Calls
    // ========================================================================
    bool CallClient(uint32_t targetClientId, const std::string& rpcName, const NetBufferWriter& args) {
        return DispatchRpc(0, targetClientId, rpcName, args.GetBuffer());
    }

    bool Broadcast(const std::string& rpcName, const NetBufferWriter& args) {
        return DispatchRpc(0, 0xFFFFFFFF, rpcName, args.GetBuffer());
    }

    bool Broadcast(const std::string& rpcName) {
        NetBufferWriter empty;
        return Broadcast(rpcName, empty);
    }

    // ========================================================================
    // Packet Framing & Native Socket Interception
    // ========================================================================
    // Frames RPC data with 0xD7EA magic header for transmission
    static std::vector<uint8_t> PackRpcFrame(uint32_t callerId, uint32_t targetId, uint64_t rpcHash, const std::vector<uint8_t>& payload) {
        NetBufferWriter writer;
        writer.WriteUInt32(DREAM_RPC_MAGIC);
        writer.WriteUInt32(callerId);
        writer.WriteUInt32(targetId);
        writer.WriteUInt64(rpcHash);
        writer.WriteUInt32(static_cast<uint32_t>(payload.size()));
        if (!payload.empty()) {
            writer.WriteBytes(payload.data(), payload.size());
        }
        return writer.GetBuffer();
    }

    // Inspects raw incoming network buffer.
    // trustedSenderId: The authenticated connection/peer ID from the server transport.
    // Overrides frameCallerId to prevent client impersonation of Server (ID=0) or other clients.
    // Returns true if packet was a Dream RPC and was consumed.
    bool ProcessIncomingRaw(const uint8_t* data, size_t size, uint32_t trustedSenderId = 0xFFFFFFFF) {
        if (!data || size < 24) return false; // Minimum header size: 4 + 4 + 4 + 8 + 4 = 24

        NetBufferReader reader(data, size);
        uint32_t magic = reader.ReadUInt32();
        if (magic != DREAM_RPC_MAGIC) return false;

        uint32_t frameCallerId = reader.ReadUInt32();
        uint32_t targetId = reader.ReadUInt32();
        uint64_t rpcHash = reader.ReadUInt64();
        uint32_t payloadSize = reader.ReadUInt32();

        // Anti-Spoofing: Prioritize trustedSenderId from authenticated transport connection
        uint32_t callerId = (trustedSenderId != 0xFFFFFFFF) ? trustedSenderId : frameCallerId;

        // Anti-Spoofing Validation: Block immediately on ANY mismatch between frame caller and actual transport
        if (trustedSenderId != 0xFFFFFFFF && frameCallerId != trustedSenderId) {
            DREAM_LOG_WARN("DreamRpcMultiplexer: Spoofing attempt blocked! Frame claimed caller %u, actual transport %u",
                           frameCallerId, trustedSenderId);
            return true; // Consumed and blocked immediately
        }

        // Per-Caller Rate Limiting (Protects Dedicated Server from RPC flooding)
        if (callerId > 0) {
            auto now = std::chrono::steady_clock::now();
            std::lock_guard<std::mutex> lock(m_mutex);
            auto& rl = m_rateLimits[callerId];
            auto elapsedSec = std::chrono::duration_cast<std::chrono::seconds>(now - rl.windowStart).count();
            if (elapsedSec >= 1) {
                rl.windowStart = now;
                rl.count = 0;
            }
            if (++rl.count > m_maxCallsPerSecond) {
                DREAM_LOG_WARN("DreamRpcMultiplexer: Rate limit exceeded for caller %u (%u calls/sec, limit: %u)",
                               callerId, rl.count, m_maxCallsPerSecond);
                return true; // Consumed and rate-limited
            }
        }

        // Server-Side Security Cap: Reject oversized/malicious packets (Senior Dev Q4)
        constexpr uint32_t MAX_RPC_PAYLOAD = 64 * 1024; // 64 KB
        if (payloadSize > MAX_RPC_PAYLOAD || reader.GetRemaining() < payloadSize) {
            DREAM_LOG_WARN("DreamRpcMultiplexer: Dropped malformed/oversized RPC frame from caller %u (size: %u)",
                           callerId, payloadSize);
            return true; // Consumed and dropped
        }

        // Server-Side Permission Check:
        // Default to DENY for remote callers (callerId > 0) if no permission validator is installed!
        if (callerId > 0) {
            if (!m_permissionValidator || !m_permissionValidator(callerId, rpcHash)) {
                DREAM_LOG_WARN("DreamRpcMultiplexer: Unauthorized RPC call (Hash: 0x%016llX) from remote caller %u (default DENY)",
                               static_cast<unsigned long long>(rpcHash), callerId);
                return true; // Consumed and blocked
            }
        }

        std::vector<uint8_t> payload(payloadSize);
        if (payloadSize > 0) {
            reader.ReadBytes(payload.data(), payloadSize);
        }

        // Route to local handler
        ExecuteRpc(callerId, targetId, rpcHash, payload);
        return true;
    }

    using PermissionValidator = std::function<bool(uint32_t callerId, uint64_t rpcHash)>;
    void SetPermissionValidator(PermissionValidator v) { m_permissionValidator = v; }
    void SetMaxCallsPerSecond(uint32_t limit) { m_maxCallsPerSecond = limit; }
    uint32_t GetMaxCallsPerSecond() const { return m_maxCallsPerSecond; }

    // Set custom socket sender callback for real multiplayer transmission
    void SetRawSenderCallback(std::function<bool(const uint8_t* data, size_t size)> cb) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_rawSender = cb;
    }

    // Diagnostics
    size_t GetTotalCallsSent() const { return m_callsSent.load(); }
    size_t GetTotalCallsHandled() const { return m_callsHandled.load(); }
    void SetLocalClientId(uint32_t id) {
        m_localClientId = id;
        DreamNetBridge::Instance().SetLocalClientId(id);
    }
    uint32_t GetLocalClientId() const {
        return DreamNetBridge::Instance().GetLocalClientId();
    }

private:
    DreamRpcMultiplexer() {
        // Automatically bind to DreamNetBridge with trusted sender ID pass-through
        DreamNetBridge::Instance().RegisterHandler(
            DreamHash::Hash("DREAM_RPC_CHANNEL"), 0x0001,
            [this](const NetPacket& pkt) {
                ProcessIncomingRaw(pkt.payload.data(), pkt.payload.size(), pkt.senderId);
            }
        );
    }

    bool DispatchRpc(uint32_t callerId, uint32_t targetId, const std::string& rpcName, const std::vector<uint8_t>& payload) {
        uint64_t hash = DreamHash::Hash(rpcName);
        m_callsSent++;

        std::vector<uint8_t> frame = PackRpcFrame(callerId, targetId, hash, payload);

        // 1. If raw sender hook is configured, send via game's network
        if (m_rawSender) {
            bool sent = false;
            DreamCrashGuard::ExecuteSafely("RpcRawSend", [&]() {
                sent = m_rawSender(frame.data(), frame.size());
            });
            if (sent) return true;
        }

        // 2. Transmit via DreamNetBridge
        NetBufferWriter writer;
        writer.WriteBytes(frame.data(), frame.size());
        if (callerId == 0) {
            if (targetId == 0xFFFFFFFF) {
                DreamNetBridge::Instance().BroadcastToClients(DreamHash::Hash("DREAM_RPC_CHANNEL"), 0x0001, writer);
            } else {
                DreamNetBridge::Instance().SendToClient(targetId, DreamHash::Hash("DREAM_RPC_CHANNEL"), 0x0001, writer);
            }
        } else {
            DreamNetBridge::Instance().SendToServer(DreamHash::Hash("DREAM_RPC_CHANNEL"), 0x0001, writer);
        }

        return true;
    }

    void ExecuteRpc(uint32_t callerId, uint32_t targetId, uint64_t rpcHash, const std::vector<uint8_t>& payload) {
        RpcHandler handler = nullptr;
        std::string rpcName;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            auto it = m_handlers.find(rpcHash);
            if (it != m_handlers.end()) {
                handler = it->second;
                rpcName = m_nameRegistry[rpcHash];
            }
        }

        if (handler) {
            RpcInvocationContext ctx(callerId, targetId, rpcHash, rpcName, payload);
            DreamCrashGuard::ExecuteSafely("RpcExecution", [&]() {
                handler(ctx);
            });
            m_callsHandled++;
        } else {
            DREAM_LOG_DEBUG("DreamRpcMultiplexer: Unregistered RPC call received (Hash: 0x%016llX)",
                            static_cast<unsigned long long>(rpcHash));
        }
    }

    struct CallerRateLimit {
        std::chrono::steady_clock::time_point windowStart{std::chrono::steady_clock::now()};
        uint32_t count{0};
    };

    std::mutex m_mutex;
    std::unordered_map<uint64_t, RpcHandler> m_handlers;
    std::unordered_map<uint64_t, std::string> m_nameRegistry;
    std::unordered_map<uint32_t, CallerRateLimit> m_rateLimits;
    uint32_t m_maxCallsPerSecond{120};
    PermissionValidator m_permissionValidator{nullptr};
    std::function<bool(const uint8_t* data, size_t size)> m_rawSender{nullptr};
    uint32_t m_localClientId{1};
    std::atomic<size_t> m_callsSent{0};
    std::atomic<size_t> m_callsHandled{0};
};

} // namespace dreamkurumi
