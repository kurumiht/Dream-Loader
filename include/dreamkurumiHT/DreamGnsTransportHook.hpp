#pragma once
#include <windows.h>
#include <cstdint>
#include <cstddef>
#include "DreamCore.hpp"
#include "DreamRpcMultiplexer.hpp"
#include "DreamLog.hpp"
#include "DreamNetStateTracker.hpp"

namespace dreamkurumi {

// ============================================================================
// DreamGnsTransportHook: Out-of-Band RPC Demultiplexing for GameNetworkingSockets
//
// Solves Keen Holistic Network Desync:
// The Enshrouded game engine (keen::net) serializes state updates using bitstream
// packing (PacketWriter/PacketReader) where opcodes and fields are dynamically
// bit-aligned. Injecting custom opcodes directly into keen::net causes bit-offset drift.
//
// By demultiplexing at the boundary of ISteamNetworkingSockets (ReceiveMessagesOnConnection),
// dreamkurumiHT intercepts mod RPC frames (DREAM_RPC_MAGIC_V2 / V1) out-of-band on pre-crypto /
// post-reassembly whole datagrams, extracts the genuine authenticated connection ID
// (trustedSenderId = msg->m_conn), releases the message, and removes it from the array
// passed to keen::net with ZERO bitstream interference.
// ============================================================================

#pragma pack(push, 8)

// Standard 136-byte SteamNetworkingIdentity ABI representation (Steamworks SDK v1.46+)
struct SteamNetworkingIdentityStandard {
    uint32_t m_eType{0};
    int32_t m_cbSize{0};
    uint8_t m_reserved[128]{0};
};

static_assert(sizeof(SteamNetworkingIdentityStandard) == 136, "SteamNetworkingIdentityStandard must match Steamworks ABI (136 bytes)");

// Minimal ABI layout of SteamNetworkingMessage_t in GameNetworkingSockets / Steamworks SDK on x64
struct SteamNetworkingMessageMinimal {
    void* m_pData{nullptr};                                      // +0x00
    int m_cbSize{0};                                             // +0x08
    uint32_t m_conn{0};                                          // +0x0C (HSteamNetConnection)
    SteamNetworkingIdentityStandard m_identityPeer{};            // +0x10 (136 bytes)
    int64_t m_nConnUserData{0};                                  // +0x98
    int64_t m_usecTimeReceived{0};                               // +0xA0
    int64_t m_nMessageNumber{0};                                 // +0xA8
    void (*m_pfnFreeData)(SteamNetworkingMessageMinimal* pMsg){nullptr}; // +0xB0
    void (*m_pfnRelease)(SteamNetworkingMessageMinimal* pMsg){nullptr};  // +0xB8
    int m_nChannel{0};                                           // +0xC0
    int m_nFlags{0};                                             // +0xC4
    int64_t m_nUserData{0};                                      // +0xC8
    uint16_t m_idxLane{0};                                       // +0xD0
    uint16_t _pad16{0};                                          // +0xD2
    uint32_t _pad32{0};                                          // +0xD4

    void Release() noexcept {
        if (m_pfnRelease) {
            m_pfnRelease(this);
        }
    }
};

#pragma pack(pop)

class DreamGnsTransportHook {
private:
    static inline std::atomic<uint64_t> s_lastMessageTimeMs{0};
    static inline std::atomic<bool> s_hasActiveConnection{false};

public:
    static bool IsConnected() noexcept {
        if (!s_hasActiveConnection.load(std::memory_order_relaxed)) return false;
        uint64_t now = GetTickCount64();
        uint64_t last = s_lastMessageTimeMs.load(std::memory_order_relaxed);
        return (now >= last && (now - last) <= 10000);
    }

    static void SetConnected(bool connected) noexcept {
        s_hasActiveConnection.store(connected, std::memory_order_relaxed);
        if (connected) {
            s_lastMessageTimeMs.store(GetTickCount64(), std::memory_order_relaxed);
        }
    }

    static void RecordNetworkActivity(uint32_t connId = 0) noexcept {
        s_hasActiveConnection.store(true, std::memory_order_relaxed);
        s_lastMessageTimeMs.store(GetTickCount64(), std::memory_order_relaxed);
        DreamNetStateTracker::Instance().RecordPacketActivity(connId);
    }

    // Filters and demultiplexes incoming messages obtained from ISteamNetworkingSockets::ReceiveMessagesOnConnection.
    // Mod RPC frames matching DREAM_RPC_MAGIC_V2 (or V1) are intercepted, passed to DreamRpcMultiplexer,
    // and released immediately so the game engine (keen::net) never sees them.
    // The message array is compacted in-place and the updated message count is returned.
    static int FilterAndDemuxIncomingMessages(
        SteamNetworkingMessageMinimal** ppOutMessages,
        int numMessages,
        bool isServerSide = true)
    {
        if (!ppOutMessages || numMessages <= 0) {
            return numMessages;
        }

        RecordNetworkActivity();

        int writeIdx = 0;
        for (int readIdx = 0; readIdx < numMessages; ++readIdx) {
            SteamNetworkingMessageMinimal* msg = ppOutMessages[readIdx];
            if (!msg || !msg->m_pData || msg->m_cbSize < 24 || !MemoryHelper::IsSafeToProbeAddress(msg->m_pData, 4)) {
                ppOutMessages[writeIdx++] = msg;
                continue;
            }

            const uint32_t magic = *reinterpret_cast<const uint32_t*>(msg->m_pData);
            if (magic == DREAM_RPC_MAGIC_V2 || magic == DREAM_RPC_MAGIC_V1) {
                // Out-of-band Dream RPC packet intercepted!
                // On server side, msg->m_conn is the authenticated client connection ID.
                // On client side, trustedSenderId can be 0 (Server).
                const uint32_t trustedSenderId = isServerSide ? static_cast<uint32_t>(msg->m_conn) : 0;

                DreamRpcMultiplexer::Instance().ProcessIncomingRaw(
                    static_cast<const uint8_t*>(msg->m_pData),
                    static_cast<size_t>(msg->m_cbSize),
                    trustedSenderId
                );

                // Release message memory using GNS allocator function
                msg->Release();
                // Do not increment writeIdx; this removes the message from the stream passed to the game
                continue;
            }

            // Normal game engine packet - keep in array for keen::net
            ppOutMessages[writeIdx++] = msg;
        }

        return writeIdx;
    }
};

} // namespace dreamkurumi
