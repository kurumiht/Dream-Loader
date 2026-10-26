#pragma once
#include <windows.h>
#include <cstdint>
#include <string>
#include <vector>
#include <unordered_map>
#include <mutex>
#include <atomic>
#include <sstream>
#include <iomanip>
#include "DreamHook.hpp"
#include "DreamLog.hpp"
#include "DreamCrashGuard.hpp"

namespace dreamkurumi {

struct TraceInfo {
    uint32_t id{0};
    void* address{nullptr};
    std::string label;
    std::atomic<uint64_t> callCount{0};
    std::unique_ptr<DreamMidHook> hook;
    bool enabled{true};
};

// ============================================================================
// DreamTracer: Real-time In-Engine Non-blocking Function & Register Tracer
// Allows mod developers to inspect function parameters live without x64dbg freezes
// ============================================================================
class DreamTracer {
public:
    static DreamTracer& Instance() {
        static DreamTracer s_instance;
        return s_instance;
    }

    uint32_t AddTrace(void* address, const std::string& label = "") {
        if (!address) return 0;
        std::lock_guard<std::mutex> lock(m_mutex);

        // Check if already traced
        for (const auto& [id, t] : m_traces) {
            if (t->address == address) {
                DREAM_LOG_WARN("DreamTracer: Address 0x%p is already being traced (ID: %u)", address, id);
                return id;
            }
        }

        uint32_t id = ++m_nextId;
        auto trace = std::make_unique<TraceInfo>();
        trace->id = id;
        trace->address = address;
        trace->label = label.empty() ? ("Func_" + std::to_string(id)) : label;
        trace->hook = std::make_unique<DreamMidHook>();

        TraceInfo* pRaw = trace.get();
        bool ok = trace->hook->Install(address, [](DreamCpuContext& ctx, void* userData) {
            auto* info = reinterpret_cast<TraceInfo*>(userData);
            if (!info || !info->enabled) return;

            uint64_t count = ++info->callCount;
            // Rate limit output if high frequency (e.g. max 50 logs/sec per hook)
            static thread_local uint64_t lastLogTime = 0;
            uint64_t now = GetTickCount64();
            if (now - lastLogTime < 20 && count > 10) return;
            lastLogTime = now;

            std::ostringstream ss;
            ss << "\033[96m[TRACE #" << count << "]\033[0m "
               << info->label << " (0x" << std::hex << reinterpret_cast<uintptr_t>(info->address) << std::dec << ")\n"
               << "  RCX: " << FormatArg(ctx.rcx) << "\n"
               << "  RDX: " << FormatArg(ctx.rdx) << "\n"
               << "  R8 : " << FormatArg(ctx.r8)  << "\n"
               << "  R9 : " << FormatArg(ctx.r9)  << "\n"
               << "  XMM0: " << std::fixed << std::setprecision(3) << ctx.xmm[0].f32[0]
               << ", XMM1: " << ctx.xmm[1].f32[0] << "\n";

            std::string out = ss.str();
            DREAM_LOG_INFO("%s", out.c_str());
            printf("%s\n", out.c_str());
            fflush(stdout);
        }, pRaw);

        if (!ok) {
            DREAM_LOG_ERROR("DreamTracer: Failed to install midhook on 0x%p", address);
            return 0;
        }

        m_traces[id] = std::move(trace);
        DREAM_LOG_INFO("DreamTracer: Started tracing 0x%p (ID: %u, Label: '%s')", address, id, label.c_str());
        return id;
    }

    bool RemoveTrace(uint32_t id) {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_traces.find(id);
        if (it == m_traces.end()) return false;
        it->second->hook->Disable();
        DREAM_LOG_INFO("DreamTracer: Stopped tracing ID %u (0x%p)", id, it->second->address);
        m_traces.erase(it);
        return true;
    }

    void ClearAll() {
        std::lock_guard<std::mutex> lock(m_mutex);
        for (auto& [id, t] : m_traces) {
            t->hook->Disable();
        }
        m_traces.clear();
        DREAM_LOG_INFO("DreamTracer: Cleared all active traces.");
    }

    struct TraceSummary {
        uint32_t id;
        void* address;
        std::string label;
        uint64_t count;
    };

    std::vector<TraceSummary> GetActiveTraces() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        std::vector<TraceSummary> list;
        for (const auto& [id, t] : m_traces) {
            list.push_back({ t->id, t->address, t->label, t->callCount.load() });
        }
        return list;
    }

private:
    DreamTracer() = default;

    static std::string FormatArg(uint64_t val) {
        std::ostringstream ss;
        ss << "0x" << std::hex << val << std::dec << " (" << static_cast<int64_t>(val) << ")";

        // Check if pointer is readable string
        const void* ptr = reinterpret_cast<const void*>(val);
        if (MemoryHelper::IsValidPtr(ptr)) {
            char buf[32] = {0};
            bool hasAscii = true;
            bool readOk = false;
            DreamCrashGuard::ExecuteSafely("FormatArgRead", [&]() {
                for (size_t i = 0; i < 16; ++i) {
                    char c = reinterpret_cast<const char*>(ptr)[i];
                    if (c == '\0') {
                        buf[i] = '\0';
                        break;
                    }
                    if (c < 32 || c > 126) {
                        hasAscii = false;
                        break;
                    }
                    buf[i] = c;
                }
                readOk = true;
            });
            if (readOk && hasAscii && buf[0] != '\0') {
                ss << " -> \"" << buf << "\"";
            }
        }
        return ss.str();
    }

    mutable std::mutex m_mutex;
    uint32_t m_nextId{0};
    std::unordered_map<uint32_t, std::unique_ptr<TraceInfo>> m_traces;
};

} // namespace dreamkurumi
