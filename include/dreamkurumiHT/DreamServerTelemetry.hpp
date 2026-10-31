#pragma once
#include <windows.h>
#include <cstdint>
#include <atomic>
#include "DreamRpcMultiplexer.hpp"
#include "DreamToast.hpp"

namespace dreamkurumi {

// ============================================================================
// DreamServerTelemetry - Silent Telemetry Pipeline for Server-to-Client Alerts
// Strict Policy: 100% Visual Only, Zero Audio Chime (Dev 2 Architecture).
// Lock-free SPSC Ring Buffer in VEH -> Coalesced Broadcast on Server Tick.
// ============================================================================

#pragma pack(push, 1)
struct TelemetryAlertPacket {
    uint32_t faultCode;       // e.g. 0xC0000005
    uint32_t faultRva;        // e.g. 0x000A1394
    uint16_t occurrenceCount; // Coalesced count within flush window
    uint8_t  severity;        // 0 = Info, 1 = Warning (Recovered), 2 = Circuit Tripped
    uint8_t  visualFlags;     // Bit 0 = SilentVisualOnly (Always 1: strictly no audio)
    char     tag[20];         // Short mod/subsystem identifier
};
#pragma pack(pop)

class DreamServerTelemetry {
    static constexpr size_t RING_SIZE = 64;
    TelemetryAlertPacket m_ring[RING_SIZE]{};
    alignas(64) std::atomic<uint32_t> m_writeIdx{0};
    alignas(64) std::atomic<uint32_t> m_readIdx{0};

public:
    static DreamServerTelemetry& Instance() noexcept {
        static DreamServerTelemetry s_inst;
        return s_inst;
    }

    // Safe to call directly right after VEH recovery (~10 ns, zero locks, zero heap alloc)
    void PushRecoveredFault(uint32_t code, uint32_t rva, const char* modTag, uint8_t severity = 1) noexcept {
        const uint32_t w = m_writeIdx.load(std::memory_order_relaxed);
        const uint32_t r = m_readIdx.load(std::memory_order_acquire);
        if ((w - r) >= RING_SIZE) return; // Drop if buffer full: never stall server!

        auto& slot = m_ring[w & (RING_SIZE - 1)];
        slot.faultCode       = code;
        slot.faultRva        = rva;
        slot.occurrenceCount = 1;
        slot.severity        = severity;
        slot.visualFlags     = 0x01; // Strictly visual-only (No audio chime)
        for (size_t i = 0; i < 19 && modTag && modTag[i]; ++i) {
            slot.tag[i] = modTag[i];
            slot.tag[i + 1] = '\0';
        }

        m_writeIdx.store(w + 1, std::memory_order_release);
    }

    // Ring stats and helpers
    uint32_t GetPendingCount() const noexcept {
        const uint32_t w = m_writeIdx.load(std::memory_order_relaxed);
        const uint32_t r = m_readIdx.load(std::memory_order_acquire);
        return (w >= r) ? (w - r) : 0;
    }

    void ResetForTest() noexcept {
        m_readIdx.store(0, std::memory_order_relaxed);
        m_writeIdx.store(0, std::memory_order_release);
    }

    // Called periodically (every 250ms) on Server Tick to coalesce & broadcast
    uint32_t FlushCoalescedAlerts() {
        uint32_t r = m_readIdx.load(std::memory_order_relaxed);
        const uint32_t w = m_writeIdx.load(std::memory_order_acquire);
        if (r == w) return 0;

        uint32_t flushedCount = 0;
        // Coalesce identical RVA faults in the flush window into a single packet
        TelemetryAlertPacket merged = m_ring[r & (RING_SIZE - 1)];
        r++;
        flushedCount++;
        while (r != w) {
            const auto& next = m_ring[r & (RING_SIZE - 1)];
            if (next.faultRva == merged.faultRva && next.faultCode == merged.faultCode) {
                merged.occurrenceCount++;
            }
            r++;
            flushedCount++;
        }
        m_readIdx.store(w, std::memory_order_release);

        NetBufferWriter writer(sizeof(TelemetryAlertPacket));
        writer.WriteBytes(&merged, sizeof(TelemetryAlertPacket));

        // Broadcast over out-of-band GNS channel
        DreamRpcMultiplexer::Instance().Broadcast("DreamTelemetryAlert", writer);
        return flushedCount;
    }

    // Client-side receiver registration
    void RegisterClientReceiver() {
        DreamRpcMultiplexer::Instance().RegisterRpc("DreamTelemetryAlert", [](RpcInvocationContext& ctx) {
            if (ctx.reader.GetRemaining() < sizeof(TelemetryAlertPacket)) return;
            TelemetryAlertPacket pkt{};
            if (!ctx.reader.ReadBytes(&pkt, sizeof(TelemetryAlertPacket))) return;

            // Enforce visual-only: ZERO audio calls (purely visual toast)
            char buf[128];
            if (pkt.occurrenceCount > 1) {
                snprintf(buf, sizeof(buf), "[Server Shield] Recovered %u faults in %s (RVA 0x%X)",
                         (unsigned)pkt.occurrenceCount, pkt.tag, (unsigned)pkt.faultRva);
            } else {
                snprintf(buf, sizeof(buf), "[Server Shield] Recovered fault in %s (RVA 0x%X)",
                         pkt.tag, (unsigned)pkt.faultRva);
            }
            // Display amber warning toast (Color: 0xFFAA00) for 3.0 seconds
            DreamToast::Instance().Show(buf, 3.0f, 0xFFAA00);
        });
    }
};

} // namespace dreamkurumi
