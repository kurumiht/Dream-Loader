#pragma once
#include <chrono>
#include <string>
#include <unordered_map>
#include <mutex>
#include <atomic>
#include <vector>
#include <algorithm>
#include <cstdio>
#include "DreamLog.hpp"

namespace dreamkurumi {

// ============================================================================
// DreamProfiler - High-Precision Hook & Game Loop Micro-Profiler
// Detects micro-stutter, slow hooks, and measures execution time in nanoseconds
// ============================================================================
struct ProfileStats {
    std::string name;
    uint64_t callCount{0};
    uint64_t totalNs{0};
    uint64_t minNs{UINT64_MAX};
    uint64_t maxNs{0};

    double GetAvgUs() const {
        if (callCount == 0) return 0.0;
        return (static_cast<double>(totalNs) / callCount) / 1000.0;
    }
    double GetMinUs() const {
        if (minNs == UINT64_MAX) return 0.0;
        return minNs / 1000.0;
    }
    double GetMaxUs() const {
        return maxNs / 1000.0;
    }
};

class DreamProfiler {
public:
    static DreamProfiler& Instance() {
        static DreamProfiler s_instance;
        return s_instance;
    }

    void SetEnabled(bool enable) { m_enabled.store(enable); }
    bool IsEnabled() const { return m_enabled.load(); }

    void SetSlowWarningThresholdNs(uint64_t ns) { m_warningThresholdNs.store(ns); }
    uint64_t GetSlowWarningThresholdNs() const { return m_warningThresholdNs.load(); }

    void Record(const std::string& name, uint64_t elapsedNs) {
        if (!m_enabled.load()) return;

        uint64_t threshold = m_warningThresholdNs.load();
        if (threshold > 0 && elapsedNs > threshold) {
            DREAM_LOG_WARN("DreamProfiler: Slow execution in '%s'! Took %.3f ms (Threshold: %.3f ms)",
                           name.c_str(), elapsedNs / 1000000.0, threshold / 1000000.0);
        }

        std::lock_guard<std::mutex> lock(m_mutex);
        auto& stat = m_stats[name];
        stat.name = name;
        stat.callCount++;
        stat.totalNs += elapsedNs;
        if (elapsedNs < stat.minNs) stat.minNs = elapsedNs;
        if (elapsedNs > stat.maxNs) stat.maxNs = elapsedNs;
    }

    ProfileStats GetStats(const std::string& name) {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_stats.find(name);
        if (it != m_stats.end()) {
            return it->second;
        }
        return ProfileStats{name};
    }

    void Reset() {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_stats.clear();
        DREAM_LOG_INFO("DreamProfiler: Profiler stats cleared");
    }

    void PrintReport() {
        std::lock_guard<std::mutex> lock(m_mutex);
        printf("\n\033[95m============================= HOOK PROFILER REPORT =============================\033[0m\n");
        printf(" \033[97m%-28s %-10s %-12s %-12s %-12s\033[0m\n", "Hook / Function Name", "Calls", "Avg (us)", "Min (us)", "Max (us)");
        printf("---------------------------------------------------------------------------------\n");
        if (m_stats.empty()) {
            printf("  (No profiled data recorded yet)\n");
        } else {
            std::vector<ProfileStats> list;
            for (const auto& [k, v] : m_stats) list.push_back(v);
            std::sort(list.begin(), list.end(), [](const ProfileStats& a, const ProfileStats& b) {
                return a.totalNs > b.totalNs;
            });
            for (const auto& s : list) {
                printf("  %-26s %-10llu %-12.2f %-12.2f %-12.2f\n",
                       s.name.c_str(),
                       static_cast<unsigned long long>(s.callCount),
                       s.GetAvgUs(),
                       s.GetMinUs(),
                       s.GetMaxUs());
            }
        }
        printf("\033[95m=================================================================================\033[0m\n\n");
        fflush(stdout);
    }

private:
    DreamProfiler() = default;

    std::atomic<bool> m_enabled{true};
    std::atomic<uint64_t> m_warningThresholdNs{500000}; // Default 500us (0.5ms)
    std::mutex m_mutex;
    std::unordered_map<std::string, ProfileStats> m_stats;
};

// RAII Scoped Profiler Helper
class ScopedProfile {
public:
    ScopedProfile(const std::string& name) : m_name(name) {
        if (DreamProfiler::Instance().IsEnabled()) {
            m_start = std::chrono::high_resolution_clock::now();
        }
    }

    ~ScopedProfile() {
        if (DreamProfiler::Instance().IsEnabled()) {
            auto end = std::chrono::high_resolution_clock::now();
            uint64_t ns = std::chrono::duration_cast<std::chrono::nanoseconds>(end - m_start).count();
            DreamProfiler::Instance().Record(m_name, ns);
        }
    }

private:
    std::string m_name;
    std::chrono::high_resolution_clock::time_point m_start;
};

#define DREAM_PROFILE_SCOPE(name) ::dreamkurumi::ScopedProfile _dream_prof_##__LINE__(name)

} // namespace dreamkurumi
