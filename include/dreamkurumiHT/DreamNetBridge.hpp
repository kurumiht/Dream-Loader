#pragma once
#include <windows.h>
#include <cstdint>
#include <string>
#include <vector>
#include <unordered_map>
#include <functional>
#include <mutex>
#include <cstring>
#include <memory>
#include <atomic>
#include "DreamLog.hpp"
#include "DreamHash.hpp"
#include "DreamEnv.hpp"
#include "DreamCrashGuard.hpp"

namespace dreamkurumi {

// ============================================================================
// Packet Header Constants
// ============================================================================
constexpr uint32_t DREAM_NET_MAGIC = 0x4452454D; // "DREM"

#pragma pack(push, 1)
struct NetPacketHeader {
    uint32_t magic{DREAM_NET_MAGIC};
    uint64_t channelHash{0};
    uint16_t opcode{0};
    uint32_t senderId{0};       // 0 = Server, >0 = Client Player ID
    uint32_t targetId{0};       // 0 = Broadcast / Server, >0 = Target Client ID
    uint32_t payloadSize{0};
};
#pragma pack(pop)

// ============================================================================
// NetPacket - Decoded In-Memory Mod Packet
// ============================================================================
struct NetPacket {
    uint64_t channelHash{0};
    uint16_t opcode{0};
    uint32_t senderId{0};
    uint32_t targetId{0};
    std::vector<uint8_t> payload;

    bool IsFromServer() const { return senderId == 0; }
    bool IsFromClient() const { return senderId > 0; }
};

// ============================================================================
// NetBufferWriter - High-Performance Binary Serialization for Mod Packets
// Safe, little-endian, zero-allocation growth buffer
// ============================================================================
class NetBufferWriter {
public:
    NetBufferWriter(size_t initialCapacity = 256) {
        m_buffer.reserve(initialCapacity);
    }

    void WriteUInt8(uint8_t val) {
        m_buffer.push_back(val);
    }

    void WriteInt8(int8_t val) {
        WriteUInt8(static_cast<uint8_t>(val));
    }

    void WriteUInt16(uint16_t val) {
        AppendPod(val);
    }

    void WriteInt16(int16_t val) {
        AppendPod(val);
    }

    void WriteUInt32(uint32_t val) {
        AppendPod(val);
    }

    void WriteInt32(int32_t val) {
        AppendPod(val);
    }

    void WriteUInt64(uint64_t val) {
        AppendPod(val);
    }

    void WriteInt64(int64_t val) {
        AppendPod(val);
    }

    void WriteFloat(float val) {
        AppendPod(val);
    }

    void WriteDouble(double val) {
        AppendPod(val);
    }

    void WriteBool(bool val) {
        WriteUInt8(val ? 1 : 0);
    }

    void WriteString(const std::string& str) {
        uint16_t len = static_cast<uint16_t>((str.size() > 0xFFFF) ? 0xFFFF : str.size());
        WriteUInt16(len);
        if (len > 0) {
            WriteBytes(str.data(), len);
        }
    }

    void WriteBytes(const void* data, size_t size) {
        if (!data || size == 0) return;
        const auto* ptr = static_cast<const uint8_t*>(data);
        m_buffer.insert(m_buffer.end(), ptr, ptr + size);
    }

    const std::vector<uint8_t>& GetBuffer() const { return m_buffer; }
    const uint8_t* GetData() const { return m_buffer.data(); }
    size_t GetSize() const { return m_buffer.size(); }

    void Clear() { m_buffer.clear(); }

private:
    template <typename T>
    void AppendPod(T val) {
        static_assert(std::is_trivially_copyable_v<T>, "Type must be trivially copyable");
        uint8_t bytes[sizeof(T)];
        std::memcpy(bytes, &val, sizeof(T));
        m_buffer.insert(m_buffer.end(), bytes, bytes + sizeof(T));
    }

    std::vector<uint8_t> m_buffer;
};

// ============================================================================
// NetBufferReader - Safe, Bound-Checked Binary Deserialization
// ============================================================================
class NetBufferReader {
public:
    NetBufferReader(const uint8_t* data, size_t size)
        : m_data(data), m_size(size), m_offset(0) {}

    explicit NetBufferReader(const std::vector<uint8_t>& buf)
        : m_data(buf.data()), m_size(buf.size()), m_offset(0) {}

    uint8_t ReadUInt8() {
        if (m_offset + 1 > m_size) {
            m_error = true;
            return 0;
        }
        return m_data[m_offset++];
    }

    int8_t ReadInt8() {
        return static_cast<int8_t>(ReadUInt8());
    }

    uint16_t ReadUInt16() {
        return ReadPod<uint16_t>(0);
    }

    int16_t ReadInt16() {
        return ReadPod<int16_t>(0);
    }

    uint32_t ReadUInt32() {
        return ReadPod<uint32_t>(0);
    }

    int32_t ReadInt32() {
        return ReadPod<int32_t>(0);
    }

    uint64_t ReadUInt64() {
        return ReadPod<uint64_t>(0);
    }

    int64_t ReadInt64() {
        return ReadPod<int64_t>(0);
    }

    float ReadFloat() {
        return ReadPod<float>(0.0f);
    }

    double ReadDouble() {
        return ReadPod<double>(0.0);
    }

    bool ReadBool() {
        return ReadUInt8() != 0;
    }

    std::string ReadString() {
        uint16_t len = ReadUInt16();
        if (m_error) return "";
        if (len == 0) return "";
        if (m_offset + len > m_size) {
            m_error = true;
            return "";
        }
        std::string str(reinterpret_cast<const char*>(m_data + m_offset), len);
        m_offset += len;
        return str;
    }

    bool ReadBytes(void* dest, size_t size) {
        if (!dest || m_offset + size > m_size) {
            m_error = true;
            return false;
        }
        std::memcpy(dest, m_data + m_offset, size);
        m_offset += size;
        return true;
    }

    size_t GetRemaining() const {
        return (m_offset < m_size) ? (m_size - m_offset) : 0;
    }

    bool IsEOF() const {
        return m_offset >= m_size;
    }

    bool HasError() const {
        return m_error;
    }

    void ClearError() {
        m_error = false;
    }

    size_t GetOffset() const { return m_offset; }

private:
    template <typename T>
    T ReadPod(T fallback) {
        static_assert(std::is_trivially_copyable_v<T>, "Type must be trivially copyable");
        if (m_offset + sizeof(T) > m_size) {
            m_error = true;
            return fallback;
        }
        T val;
        std::memcpy(&val, m_data + m_offset, sizeof(T));
        m_offset += sizeof(T);
        return val;
    }

    const uint8_t* m_data{nullptr};
    size_t m_size{0};
    size_t m_offset{0};
    bool m_error{false};
};

// ============================================================================
// DreamNetBridge - Client <-> Dedicated Server Mod Communication Engine
// Features:
// - Unified dual-side packet dispatch (Client-to-Server, Server-to-Client, Broadcast)
// - FNV-1a 64-bit channel hashing for collision-free mod multiplexing
// - Automated Loopback router for singleplayer & testing
// - Serialized raw byte framing compatible with native network packet wrapping
// ============================================================================
class DreamNetBridge {
public:
    using PacketHandler = std::function<void(const NetPacket& packet)>;

    static DreamNetBridge& Instance() {
        static DreamNetBridge s_instance;
        return s_instance;
    }

    // Register a callback for a specific channel and opcode
    void RegisterHandler(uint64_t channelHash, uint16_t opcode, PacketHandler handler) {
        std::lock_guard<std::mutex> lock(m_mutex);
        uint64_t key = (channelHash ^ (static_cast<uint64_t>(opcode) << 48));
        m_handlers[key] = handler;
        DREAM_LOG_DEBUG("DreamNetBridge: Registered handler for channel 0x%016llX, opcode 0x%04X",
                        static_cast<unsigned long long>(channelHash), opcode);
    }

    // Convenience overload using string literal channel name
    void RegisterHandler(const std::string& channelName, uint16_t opcode, PacketHandler handler) {
        RegisterHandler(DreamHash::Hash(channelName), opcode, handler);
    }

    // Unregister handler
    void UnregisterHandler(uint64_t channelHash, uint16_t opcode) {
        std::lock_guard<std::mutex> lock(m_mutex);
        uint64_t key = (channelHash ^ (static_cast<uint64_t>(opcode) << 48));
        m_handlers.erase(key);
    }

    // Send packet from Client to Dedicated Server
    bool SendToServer(uint64_t channelHash, uint16_t opcode, const NetBufferWriter& writer) {
        NetPacket pkt;
        pkt.channelHash = channelHash;
        pkt.opcode = opcode;
        pkt.senderId = m_localClientId; // My player ID
        pkt.targetId = 0;               // 0 = Server
        pkt.payload = writer.GetBuffer();

        return DispatchPacket(pkt);
    }

    bool SendToServer(const std::string& channelName, uint16_t opcode, const NetBufferWriter& writer) {
        return SendToServer(DreamHash::Hash(channelName), opcode, writer);
    }

    // Send packet from Dedicated Server to specific Client
    bool SendToClient(uint32_t clientId, uint64_t channelHash, uint16_t opcode, const NetBufferWriter& writer) {
        NetPacket pkt;
        pkt.channelHash = channelHash;
        pkt.opcode = opcode;
        pkt.senderId = 0;              // 0 = From Server
        pkt.targetId = clientId;
        pkt.payload = writer.GetBuffer();

        return DispatchPacket(pkt);
    }

    bool SendToClient(uint32_t clientId, const std::string& channelName, uint16_t opcode, const NetBufferWriter& writer) {
        return SendToClient(clientId, DreamHash::Hash(channelName), opcode, writer);
    }

    // Broadcast packet from Dedicated Server to all connected Clients
    bool BroadcastToClients(uint64_t channelHash, uint16_t opcode, const NetBufferWriter& writer) {
        NetPacket pkt;
        pkt.channelHash = channelHash;
        pkt.opcode = opcode;
        pkt.senderId = 0;              // 0 = From Server
        pkt.targetId = 0xFFFFFFFF;     // 0xFFFFFFFF = All Clients
        pkt.payload = writer.GetBuffer();

        return DispatchPacket(pkt);
    }

    bool BroadcastToClients(const std::string& channelName, uint16_t opcode, const NetBufferWriter& writer) {
        return BroadcastToClients(DreamHash::Hash(channelName), opcode, writer);
    }

    // Frame a NetPacket into serialized binary bytes (for native socket transmission)
    static std::vector<uint8_t> SerializePacket(const NetPacket& pkt) {
        NetPacketHeader hdr;
        hdr.magic = DREAM_NET_MAGIC;
        hdr.channelHash = pkt.channelHash;
        hdr.opcode = pkt.opcode;
        hdr.senderId = pkt.senderId;
        hdr.targetId = pkt.targetId;
        hdr.payloadSize = static_cast<uint32_t>(pkt.payload.size());

        std::vector<uint8_t> result(sizeof(NetPacketHeader) + pkt.payload.size());
        std::memcpy(result.data(), &hdr, sizeof(NetPacketHeader));
        if (!pkt.payload.empty()) {
            std::memcpy(result.data() + sizeof(NetPacketHeader), pkt.payload.data(), pkt.payload.size());
        }
        return result;
    }

    // Deserialize incoming raw bytes from socket into a NetPacket
    // trustedSenderId: Connection/peer ID authenticated by server transport.
    // If specified (!= 0xFFFFFFFF), overrides unauthenticated senderId in packet header to prevent spoofing.
    static bool DeserializePacket(const uint8_t* data, size_t size, NetPacket& outPacket, uint32_t trustedSenderId = 0xFFFFFFFF) {
        if (!data || size < sizeof(NetPacketHeader)) return false;

        NetPacketHeader hdr;
        std::memcpy(&hdr, data, sizeof(NetPacketHeader));
        if (hdr.magic != DREAM_NET_MAGIC) return false;
        if (size < sizeof(NetPacketHeader) + hdr.payloadSize) return false;

        outPacket.channelHash = hdr.channelHash;
        outPacket.opcode = hdr.opcode;
        outPacket.senderId = (trustedSenderId != 0xFFFFFFFF) ? trustedSenderId : hdr.senderId;
        outPacket.targetId = hdr.targetId;
        outPacket.payload.assign(data + sizeof(NetPacketHeader), data + sizeof(NetPacketHeader) + hdr.payloadSize);
        return true;
    }

    // Ingest incoming raw network bytes and route to subscribed mod handlers
    bool IngestRawBytes(const uint8_t* data, size_t size, uint32_t trustedSenderId = 0xFFFFFFFF) {
        NetPacket pkt;
        if (DeserializePacket(data, size, pkt, trustedSenderId)) {
            DeliverPacket(pkt);
            return true;
        }
        return false;
    }

    // Direct delivery of packet to registered handler
    void DeliverPacket(const NetPacket& pkt) {
        PacketHandler handler = nullptr;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            uint64_t key = (pkt.channelHash ^ (static_cast<uint64_t>(pkt.opcode) << 48));
            auto it = m_handlers.find(key);
            if (it != m_handlers.end()) {
                handler = it->second;
            }
        }

        if (handler) {
            DreamCrashGuard::ExecuteSafely("NetBridge:Handler", [&]() {
                handler(pkt);
            });
            m_totalPacketsDelivered.fetch_add(1, std::memory_order_relaxed);
        } else {
            DREAM_LOG_DEBUG("DreamNetBridge: Unhandled packet on channel 0x%016llX, opcode 0x%04X",
                            static_cast<unsigned long long>(pkt.channelHash), pkt.opcode);
        }
    }

    // Configure loopback (default: enabled for local testing and singleplayer)
    void SetLoopbackEnabled(bool enabled) {
        m_loopbackEnabled = enabled;
    }

    bool IsLoopbackEnabled() const {
        return m_loopbackEnabled;
    }

    void SetLocalClientId(uint32_t id) {
        m_localClientId = id;
    }

    uint32_t GetLocalClientId() const {
        return m_localClientId;
    }

    size_t GetTotalPacketsSent() const { return m_totalPacketsSent.load(std::memory_order_relaxed); }
    size_t GetTotalPacketsDelivered() const { return m_totalPacketsDelivered.load(std::memory_order_relaxed); }

private:
    DreamNetBridge() = default;

    bool DispatchPacket(const NetPacket& pkt) {
        m_totalPacketsSent.fetch_add(1, std::memory_order_relaxed);
        if (m_loopbackEnabled) {
            // Do not loop back dedicated server broadcasts to the server itself
            if (DreamEnv::IsDedicatedServer() && pkt.senderId == 0 && pkt.targetId == 0xFFFFFFFF) {
                return true;
            }
            // Immediate local loopback dispatch
            DeliverPacket(pkt);
            return true;
        }
        // In full network mode, this attaches to the game's packet sender
        return true;
    }

    std::unordered_map<uint64_t, PacketHandler> m_handlers;
    std::mutex m_mutex;
    bool m_loopbackEnabled{true};
    uint32_t m_localClientId{1}; // Default local player ID
    std::atomic<size_t> m_totalPacketsSent{0};
    std::atomic<size_t> m_totalPacketsDelivered{0};
};

// ============================================================================
// DreamServerSync - Automated Dedicated Server -> Client Mod Config Synchronization
// & Mod Version Handshake Gate (BepInEx ServerSync style)
// ============================================================================
enum class HandshakeStatus : uint8_t {
    Accepted = 0,
    RejectedMismatch = 1,
    RejectedMissingMod = 2,
    RejectedOutdatedCore = 3
};

struct ModRequirement {
    std::string modName;
    std::string minVersion;
    std::string maxVersion;
};

struct ClientModInfo {
    std::string modName;
    std::string version;
};

struct HandshakeResponseData {
    HandshakeStatus status{HandshakeStatus::Accepted};
    std::string rejectReason;
    std::unordered_map<std::string, std::string> enforcedConfigs;
};

class DreamServerSync {
public:
    static constexpr uint64_t SYNC_CHANNEL = DreamHash::Hash("dream_server_sync");
    static constexpr uint16_t OPCODE_HANDSHAKE_SETTINGS = 0x0001;
    static constexpr uint16_t OPCODE_HANDSHAKE_REQ      = 0x0002;
    static constexpr uint16_t OPCODE_HANDSHAKE_RESP     = 0x0003;
    static constexpr uint16_t OPCODE_CONFIG_LOCK        = 0x0004;
    static constexpr uint16_t OPCODE_CONFIG_UNLOCK      = 0x0005;
    static constexpr uint16_t OPCODE_SESSION_EPOCH_ANNOUNCE = 0x0010;
    static constexpr uint16_t OPCODE_SESSION_EPOCH_QUERY    = 0x0011;

    static DreamServerSync& Instance() {
        static DreamServerSync s_instance;
        return s_instance;
    }

    // Helper to compare semantic versions e.g. "1.4.0" vs "1.5.0"
    static int CompareVersions(const std::string& v1, const std::string& v2) {
        int maj1 = 0, min1 = 0, pat1 = 0;
        int maj2 = 0, min2 = 0, pat2 = 0;
        std::sscanf(v1.c_str(), "%d.%d.%d", &maj1, &min1, &pat1);
        std::sscanf(v2.c_str(), "%d.%d.%d", &maj2, &min2, &pat2);
        if (maj1 != maj2) return maj1 < maj2 ? -1 : 1;
        if (min1 != min2) return min1 < min2 ? -1 : 1;
        if (pat1 != pat2) return pat1 < pat2 ? -1 : 1;
        return 0;
    }

    // ========================================================================
    // Server-side: Required Mod Rules & Enforced Configs
    // ========================================================================
    void RequireMod(const std::string& modName, const std::string& minVersion, const std::string& maxVersion = "") {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_requirements[modName] = {modName, minVersion, maxVersion};
        DREAM_LOG_INFO("DreamServerSync: Server requires mod '%s' (>= %s)", modName.c_str(), minVersion.c_str());
    }

    void SetEnforcedConfig(const std::string& key, const std::string& value) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_enforcedConfigs[key] = value;
    }

    void SetEnforcedConfig(const std::string& key, float value) {
        SetEnforcedConfig(key, std::to_string(value));
    }

    void SetEnforcedConfig(const std::string& key, int value) {
        SetEnforcedConfig(key, std::to_string(value));
    }

    HandshakeResponseData EvaluateHandshake(uint32_t clientCoreVersion, const std::vector<ClientModInfo>& clientMods) {
        std::lock_guard<std::mutex> lock(m_mutex);
        HandshakeResponseData resp;

        // 1. Verify Core Version (minimum 3.9.0 = 0x00030900)
        constexpr uint32_t MIN_CORE_VERSION = 0x00030900;
        if (clientCoreVersion < MIN_CORE_VERSION) {
            resp.status = HandshakeStatus::RejectedOutdatedCore;
            resp.rejectReason = "Dream Core version outdated. Dedicated server requires >= 3.9.0";
            return resp;
        }

        // 2. Check each required mod
        std::unordered_map<std::string, std::string> clientModMap;
        for (const auto& m : clientMods) {
            clientModMap[m.modName] = m.version;
        }

        for (const auto& [reqName, req] : m_requirements) {
            auto it = clientModMap.find(reqName);
            if (it == clientModMap.end()) {
                resp.status = HandshakeStatus::RejectedMissingMod;
                resp.rejectReason = "Missing required mod: '" + reqName + "' (required >= " + req.minVersion + ")";
                return resp;
            }

            if (!req.minVersion.empty() && CompareVersions(it->second, req.minVersion) < 0) {
                resp.status = HandshakeStatus::RejectedMismatch;
                resp.rejectReason = "Mod version mismatch for '" + reqName + "': server requires >= " +
                                    req.minVersion + ", client has " + it->second;
                return resp;
            }

            if (!req.maxVersion.empty() && CompareVersions(it->second, req.maxVersion) > 0) {
                resp.status = HandshakeStatus::RejectedMismatch;
                resp.rejectReason = "Mod version mismatch for '" + reqName + "': server requires <= " +
                                    req.maxVersion + ", client has " + it->second;
                return resp;
            }
        }

        // 3. Accepted! Attach enforced configs
        resp.status = HandshakeStatus::Accepted;
        resp.enforcedConfigs = m_enforcedConfigs;
        return resp;
    }

    // ========================================================================
    // Client-side: Local Mod Registration & Handshake Evaluation
    // ========================================================================
    void RegisterLocalMod(const std::string& modName, const std::string& version) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_localMods[modName] = version;
    }

    NetBufferWriter BuildHandshakeRequest(uint32_t clientCoreVersion = 0x00030900) {
        NetBufferWriter writer;
        std::lock_guard<std::mutex> lock(m_mutex);
        writer.WriteUInt32(clientCoreVersion);
        writer.WriteUInt32(static_cast<uint32_t>(m_localMods.size()));
        for (const auto& [name, ver] : m_localMods) {
            writer.WriteString(name);
            writer.WriteString(ver);
        }
        return writer;
    }

    void HandleHandshakeResponse(const NetPacket& pkt) {
        NetBufferReader reader(pkt.payload);
        uint8_t rawStatus = reader.ReadUInt8();
        auto status = static_cast<HandshakeStatus>(rawStatus);
        std::string reason = reader.ReadString();

        uint32_t configCount = reader.ReadUInt32();
        std::unordered_map<std::string, std::string> enforced;
        for (uint32_t i = 0; i < configCount; ++i) {
            std::string k = reader.ReadString();
            std::string v = reader.ReadString();
            enforced[k] = v;
        }

        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_handshakeAccepted = (status == HandshakeStatus::Accepted);
            m_rejectionReason = reason;
        }

        if (status == HandshakeStatus::Accepted) {
            DREAM_LOG_INFO("DreamServerSync: Dedicated Server accepted handshake. Applying %u enforced configs...", configCount);
            for (const auto& [k, v] : enforced) {
                if (s_configLocker) {
                    s_configLocker(k, v);
                }
            }
        } else {
            DREAM_LOG_WARN("DreamServerSync: Handshake rejected by Dedicated Server: %s", reason.c_str());
        }

        if (m_onHandshakeCompleted) {
            m_onHandshakeCompleted(status == HandshakeStatus::Accepted, reason);
        }
    }

    bool IsHandshakeAccepted() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_handshakeAccepted;
    }

    std::string GetRejectionReason() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_rejectionReason;
    }

    void OnDisconnectedFromServer() {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_handshakeAccepted = false;
            m_rejectionReason.clear();
        }
        if (s_configUnlocker) {
            s_configUnlocker();
        }
        DREAM_LOG_INFO("DreamServerSync: Disconnected from Dedicated Server. Restored local configs.");
    }

    static void RegisterConfigLockHandler(
        std::function<void(const std::string&, const std::string&)> locker,
        std::function<void()> unlocker)
    {
        s_configLocker = locker;
        s_configUnlocker = unlocker;
    }

    void SetOnHandshakeCompletedCallback(std::function<void(bool accepted, const std::string& reason)> cb) {
        m_onHandshakeCompleted = cb;
    }

    // ========================================================================
    // Legacy SyncVar APIs (Maintained for full backward compatibility)
    // ========================================================================
    void SetSyncVar(const std::string& key, const std::string& value) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_syncedVars[key] = value;
    }

    void SetSyncVar(const std::string& key, int value) {
        SetSyncVar(key, std::to_string(value));
    }

    void SetSyncVar(const std::string& key, float value) {
        SetSyncVar(key, std::to_string(value));
    }

    std::string GetSyncVar(const std::string& key, const std::string& defaultVal = "") {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_syncedVars.find(key);
        return (it != m_syncedVars.end()) ? it->second : defaultVal;
    }

    int GetSyncVarInt(const std::string& key, int defaultVal = 0) {
        std::string s = GetSyncVar(key, "");
        if (s.empty()) return defaultVal;
        try { return std::stoi(s); } catch (...) { return defaultVal; }
    }

    float GetSyncVarFloat(const std::string& key, float defaultVal = 0.0f) {
        std::string s = GetSyncVar(key, "");
        if (s.empty()) return defaultVal;
        try { return std::stof(s); } catch (...) { return defaultVal; }
    }

    void BroadcastServerSettings() {
        NetBufferWriter writer;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            writer.WriteUInt32(static_cast<uint32_t>(m_syncedVars.size()));
            for (const auto& [k, v] : m_syncedVars) {
                writer.WriteString(k);
                writer.WriteString(v);
            }
        }
        DreamNetBridge::Instance().BroadcastToClients(SYNC_CHANNEL, OPCODE_HANDSHAKE_SETTINGS, writer);
        DREAM_LOG_INFO("DreamServerSync: Broadcasted %zu synced server settings to clients",
                       m_syncedVars.size());
    }

    void InitializeClientListener() {
        DreamNetBridge::Instance().RegisterHandler(SYNC_CHANNEL, OPCODE_HANDSHAKE_SETTINGS, [this](const NetPacket& pkt) {
            NetBufferReader reader(pkt.payload);
            uint32_t count = reader.ReadUInt32();
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                for (uint32_t i = 0; i < count; ++i) {
                    std::string k = reader.ReadString();
                    std::string v = reader.ReadString();
                    m_syncedVars[k] = v;
                }
            }
            DREAM_LOG_INFO("DreamServerSync: Received and applied %u synced variables from Dedicated Server", count);
            if (m_onSyncReceived) {
                m_onSyncReceived();
            }
        });

        DreamNetBridge::Instance().RegisterHandler(SYNC_CHANNEL, OPCODE_HANDSHAKE_RESP, [this](const NetPacket& pkt) {
            HandleHandshakeResponse(pkt);
        });
    }

    void SetOnSyncReceivedCallback(std::function<void()> cb) {
        m_onSyncReceived = cb;
    }

private:
    DreamServerSync() = default;

    std::unordered_map<std::string, std::string> m_syncedVars;
    std::unordered_map<std::string, ModRequirement> m_requirements;
    std::unordered_map<std::string, std::string> m_enforcedConfigs;
    std::unordered_map<std::string, std::string> m_localMods;
    bool m_handshakeAccepted{false};
    std::string m_rejectionReason;

    std::function<void()> m_onSyncReceived;
    std::function<void(bool, const std::string&)> m_onHandshakeCompleted;
    mutable std::mutex m_mutex;

    static inline std::function<void(const std::string&, const std::string&)> s_configLocker{nullptr};
    static inline std::function<void()> s_configUnlocker{nullptr};
};

} // namespace dreamkurumi
