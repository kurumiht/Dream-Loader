#pragma once
#include <windows.h>
#include <cstdint>
#include <deque>
#include <map>
#include <unordered_set>
#include <mutex>
#include <algorithm>

namespace dreamkurumi {

// ============================================================================
// DreamAdaptiveBatchThrottler - Dedicated Server 16-Player Dynamic Backpressure
//
// Monitors true server CPU execution time per Simulation Tick using QPC.
// Adaptively modulates batch quota (2..24 items/tick) based on engine load
// to guarantee 60 TPS (16.66ms frame budget) with 16 concurrent players.
//
// Features:
// 1. Self-Calibrating QPC Tick Monitor (no engine RVA or clamped dt dependency).
// 2. Per-Player Round-Robin Queues (prevents 1 player from starving others).
// 3. Sub-millisecond Execution Time Cutoff with automatic item re-queuing.
// ============================================================================
class DreamAdaptiveBatchThrottler {
    struct PlayerHarvestQueue {
        std::deque<uint64_t> entities;
    };

    mutable std::mutex                       m_mutex;
    std::map<uint32_t, PlayerHarvestQueue>   m_playerQueues;
    std::unordered_set<uint64_t>             m_globalQueuedEntities;
    LARGE_INTEGER                            m_qpcFreq{};
    LARGE_INTEGER                            m_tickEnterTime{};
    double                                   m_smoothedTickMs{5.0};
    double                                   m_simulatedEngineUsedMs{-1.0};

public:
    static DreamAdaptiveBatchThrottler& Instance() noexcept {
        static DreamAdaptiveBatchThrottler s_inst;
        return s_inst;
    }

    DreamAdaptiveBatchThrottler() noexcept {
        QueryPerformanceFrequency(&m_qpcFreq);
        QueryPerformanceCounter(&m_tickEnterTime);
    }

    // 1. Call at the Prefix of Server Simulation Tick
    void OnServerTickBegin() noexcept {
        QueryPerformanceCounter(&m_tickEnterTime);
    }

    // 2. Enqueue entities per player (prevents 1 player's mega-farm from starving other 15 players!)
    size_t EnqueuePlayerBatch(uint32_t playerId, const uint64_t* entityIds, size_t count) {
        if (!entityIds || count == 0) return 0;
        std::lock_guard<std::mutex> lock(m_mutex);

        auto& pq = m_playerQueues[playerId];
        size_t added = 0;
        for (size_t i = 0; i < count; ++i) {
            if (pq.entities.size() >= 128) break; // Per-player cap
            const uint64_t id = entityIds[i];
            if (id != 0 && m_globalQueuedEntities.insert(id).second) {
                pq.entities.push_back(id);
                added++;
            }
        }
        return added;
    }

    // 3. Call at Postfix of Server Simulation Tick: Dynamically scales quota & enforces sub-millisecond cutoff
    template <typename ExecuteFn>
    size_t ProcessAdaptiveTickSlice(ExecuteFn&& executeEntity) {
        LARGE_INTEGER now{};
        QueryPerformanceCounter(&now);
        const double freq = static_cast<double>(m_qpcFreq.QuadPart);

        // Measure actual CPU time already spent by Keen Engine in this tick
        double engineUsedMs = static_cast<double>(now.QuadPart - m_tickEnterTime.QuadPart) * 1000.0 / freq;
        if (m_simulatedEngineUsedMs >= 0.0) {
            engineUsedMs = m_simulatedEngineUsedMs;
            m_smoothedTickMs = m_simulatedEngineUsedMs;
        } else {
            m_smoothedTickMs = (m_smoothedTickMs * 0.8) + (engineUsedMs * 0.2); // EMA smoothing
        }

        // Dynamic Backpressure Quota Calculation (Target: 16.66ms / 60 TPS)
        size_t maxItemsThisTick = 14;
        double maxModBudgetMs   = 1.2;

        if (m_smoothedTickMs < 8.0 && engineUsedMs < 9.0) {
            maxItemsThisTick = 24;
            maxModBudgetMs   = 2.0;
        } else if (m_smoothedTickMs < 12.0 && engineUsedMs < 12.5) {
            maxItemsThisTick = 14;
            maxModBudgetMs   = 1.2;
        } else if (m_smoothedTickMs < 14.5 && engineUsedMs < 14.8) {
            maxItemsThisTick = 6;
            maxModBudgetMs   = 0.6;
        } else {
            // Heavy server load (> 14.5ms used): throttle down to minimum trickle to protect 60 TPS
            maxItemsThisTick = 2;
            maxModBudgetMs   = 0.20;
        }

        // Pull items in Round-Robin order across all active players
        struct WorkItem { uint32_t playerId; uint64_t entityId; };
        WorkItem batch[24];
        size_t   batchCount = 0;

        {
            std::lock_guard<std::mutex> lock(m_mutex);
            while (batchCount < maxItemsThisTick && !m_playerQueues.empty()) {
                bool pulledAny = false;
                for (auto it = m_playerQueues.begin(); it != m_playerQueues.end(); ) {
                    if (!it->second.entities.empty()) {
                        uint64_t eid = it->second.entities.front();
                        it->second.entities.pop_front();
                        m_globalQueuedEntities.erase(eid);
                        batch[batchCount++] = { it->first, eid };
                        pulledAny = true;
                    }
                    if (it->second.entities.empty()) {
                        it = m_playerQueues.erase(it);
                    } else {
                        ++it;
                    }
                    if (batchCount >= maxItemsThisTick) break;
                }
                if (!pulledAny) break;
            }
        }

        // Execute with strict QPC sub-millisecond cutoff
        size_t executed = 0;
        for (size_t i = 0; i < batchCount; ++i) {
            executeEntity(batch[i].playerId, batch[i].entityId);
            executed++;

            LARGE_INTEGER afterItem{};
            QueryPerformanceCounter(&afterItem);
            const double modElapsedMs = static_cast<double>(afterItem.QuadPart - now.QuadPart) * 1000.0 / freq;

            // If mod execution exceeds its time budget, re-queue remaining items for next tick!
            if (modElapsedMs >= maxModBudgetMs && (i + 1) < batchCount) {
                std::lock_guard<std::mutex> lock(m_mutex);
                for (size_t rem = batchCount - 1; rem > i; --rem) {
                    if (m_globalQueuedEntities.insert(batch[rem].entityId).second) {
                        m_playerQueues[batch[rem].playerId].entities.push_front(batch[rem].entityId);
                    }
                }
                break;
            }
        }

        return executed;
    }

    double GetSmoothedTickMs() const noexcept { return m_smoothedTickMs; }

    void SetSmoothedTickMsForTesting(double ms) noexcept { m_smoothedTickMs = ms; }
    void SetSimulatedEngineUsedMsForTesting(double ms) noexcept { m_simulatedEngineUsedMs = ms; }

    size_t GetTotalPendingCount() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_globalQueuedEntities.size();
    }

    size_t GetActivePlayerQueueCount() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_playerQueues.size();
    }

    void Clear() {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_playerQueues.clear();
        m_globalQueuedEntities.clear();
    }
};

} // namespace dreamkurumi
