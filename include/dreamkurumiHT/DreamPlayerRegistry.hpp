#pragma once
#include <string>
#include <vector>
#include <unordered_map>
#include <mutex>
#include <cstdint>
#include <functional>
#include <algorithm>
#include <cctype>
#include "DreamLog.hpp"
#include "DreamNetBridge.hpp"

namespace dreamkurumi {

struct ConnectedPlayerInfo {
    uint32_t entityId{0};
    std::string name;
    bool isAdmin{false};
    uint64_t loginTimestamp{0};
    void* pSession{nullptr};
};

class DreamPlayerRegistry {
public:
    using KickCallback = std::function<void(uint32_t, const std::string&)>;
    using BroadcastCallback = std::function<void(const std::string&)>;

    static DreamPlayerRegistry& Instance() {
        static DreamPlayerRegistry s_instance;
        return s_instance;
    }

    void OnPlayerLogin(uint32_t entityId, const std::string& name, bool isAdmin, void* pSession = nullptr) {
        std::lock_guard<std::mutex> lock(m_mutex);
        ConnectedPlayerInfo info;
        info.entityId = entityId;
        info.name = name.empty() ? ("Player_" + std::to_string(entityId)) : name;
        info.isAdmin = isAdmin;
        info.loginTimestamp = GetTickCount64();
        info.pSession = pSession;
        m_players[entityId] = info;
    }

    void OnPlayerDisconnect(uint32_t entityId) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_players.erase(entityId);
    }

    std::vector<ConnectedPlayerInfo> GetAllPlayers() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        std::vector<ConnectedPlayerInfo> list;
        for (const auto& [id, p] : m_players) {
            list.push_back(p);
        }
        return list;
    }

    size_t GetPlayerCount() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_players.size();
    }

    bool FindPlayer(const std::string& query, ConnectedPlayerInfo& outInfo) const {
        if (query.empty()) return false;
        std::lock_guard<std::mutex> lock(m_mutex);

        // 1. Try matching entity ID (Hex or Dec)
        char* endPtr = nullptr;
        unsigned long parsedId = std::strtoul(query.c_str(), &endPtr, 0);
        if (endPtr != query.c_str() && *endPtr == '\0') {
            uint32_t id = static_cast<uint32_t>(parsedId);
            auto it = m_players.find(id);
            if (it != m_players.end()) {
                outInfo = it->second;
                return true;
            }
        }

        // 2. Try case-insensitive substring search in player name
        std::string lowerQuery = ToLower(query);
        for (const auto& [id, p] : m_players) {
            std::string lowerName = ToLower(p.name);
            if (lowerName.find(lowerQuery) != std::string::npos) {
                outInfo = p;
                return true;
            }
        }

        return false;
    }

    // Broadcast a server announcement message to all players
    void Broadcast(const std::string& message) {
        if (message.empty()) return;

        // 1. Dispatch via NetBridge
        NetBufferWriter writer;
        writer.WriteString(message);
        writer.WriteUInt64(GetTickCount64());
        DreamNetBridge::Instance().BroadcastToClients("dream_broadcast", 0x0100, writer);

        // 2. Invoke custom broadcast handler if registered (e.g. UDP or game UI hook)
        if (m_broadcastCallback) {
            m_broadcastCallback(message);
        }

        DREAM_LOG_INFO("[DreamBroadcast] Announcement: \"%s\"", message.c_str());
    }

    // Kick a player by entity ID with an optional reason
    bool Kick(uint32_t entityId, const std::string& reason = "Kicked by server administrator") {
        ConnectedPlayerInfo target;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            auto it = m_players.find(entityId);
            if (it == m_players.end()) return false;
            target = it->second;
        }

        // 1. Send kick packet via NetBridge to that client
        NetBufferWriter writer;
        writer.WriteUInt32(entityId);
        writer.WriteString(reason);
        DreamNetBridge::Instance().SendToClient(entityId, "dream_broadcast", 0x0101, writer);

        // 2. Invoke native kick callback if registered
        if (m_kickCallback) {
            m_kickCallback(entityId, reason);
        }

        // 3. Remove from registry
        OnPlayerDisconnect(entityId);

        DREAM_LOG_INFO("[DreamKick] Kicked player '%s' (0x%X) - Reason: %s",
                       target.name.c_str(), entityId, reason.c_str());
        return true;
    }

    void SetKickCallback(KickCallback cb) {
        m_kickCallback = cb;
    }

    void SetBroadcastCallback(BroadcastCallback cb) {
        m_broadcastCallback = cb;
    }

private:
    DreamPlayerRegistry() {
        // Register NetBridge handler for client-side display
        DreamNetBridge::Instance().RegisterHandler("dream_broadcast", 0x0100, [](const NetPacket& pkt) {
            NetBufferReader reader(pkt.payload.data(), pkt.payload.size());
            std::string msg = reader.ReadString();
            if (!msg.empty()) {
                DREAM_LOG_INFO("[SERVER ANNOUNCEMENT] %s", msg.c_str());
                if (GetForegroundWindow() != NULL) {
                    MessageBeep(MB_ICONINFORMATION);
                }
            }
        });

        DreamNetBridge::Instance().RegisterHandler("dream_broadcast", 0x0101, [](const NetPacket& pkt) {
            NetBufferReader reader(pkt.payload.data(), pkt.payload.size());
            uint32_t kickedId = reader.ReadUInt32();
            std::string reason = reader.ReadString();
            DREAM_LOG_WARN("[DISCONNECTED] You have been kicked from the server. Reason: %s", reason.c_str());
        });
    }

    static std::string ToLower(const std::string& s) {
        std::string res = s;
        std::transform(res.begin(), res.end(), res.begin(), [](unsigned char c) { return (char)std::tolower(c); });
        return res;
    }

    mutable std::mutex m_mutex;
    std::unordered_map<uint32_t, ConnectedPlayerInfo> m_players;
    KickCallback m_kickCallback{nullptr};
    BroadcastCallback m_broadcastCallback{nullptr};
};

} // namespace dreamkurumi
