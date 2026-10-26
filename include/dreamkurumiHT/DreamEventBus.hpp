#pragma once
#include <string>
#include <vector>
#include <unordered_map>
#include <functional>
#include <mutex>
#include <cstdint>
#include <atomic>
#include "DreamLog.hpp"
#include "DreamCrashGuard.hpp"

namespace dreamkurumi {

struct DreamEvent {
    std::string name;
    const void* data{nullptr};
    size_t size{0};
    uint64_t timestamp{0};

    // Helper: read as string if payload is text
    std::string GetString() const {
        if (!data || size == 0) return "";
        return std::string(reinterpret_cast<const char*>(data), size);
    }

    template <typename T>
    const T* GetData() const {
        if (!data || size < sizeof(T)) return nullptr;
        return reinterpret_cast<const T*>(data);
    }
};

using DreamEventCallback = std::function<void(const DreamEvent&)>;

// ============================================================================
// DreamEventBus: High-performance Pub/Sub Event System
// Decouples mods allowing them to communicate without direct DLL dependencies
// ============================================================================
class DreamEventBus {
public:
    static DreamEventBus& Instance() {
        static DreamEventBus s_instance;
        return s_instance;
    }

    // Subscribe to an event topic by name (returns subscription token ID)
    uint64_t Subscribe(const std::string& eventName, DreamEventCallback callback) {
        if (!callback || eventName.empty()) return 0;
        std::lock_guard<std::mutex> lock(m_mutex);
        uint64_t id = ++m_nextHandlerId;
        m_subscribers[eventName].push_back({ id, callback });
        return id;
    }

    // Unsubscribe using the token ID returned from Subscribe
    bool Unsubscribe(uint64_t handlerId) {
        if (handlerId == 0) return false;
        std::lock_guard<std::mutex> lock(m_mutex);
        for (auto& [name, list] : m_subscribers) {
            for (auto it = list.begin(); it != list.end(); ++it) {
                if (it->id == handlerId) {
                    list.erase(it);
                    return true;
                }
            }
        }
        return false;
    }

    // Publish an event with optional binary payload
    size_t Publish(const std::string& eventName, const void* data = nullptr, size_t size = 0) {
        if (eventName.empty()) return 0;
        std::vector<DreamEventCallback> callbacksToInvoke;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            auto it = m_subscribers.find(eventName);
            if (it != m_subscribers.end()) {
                for (const auto& sub : it->second) {
                    callbacksToInvoke.push_back(sub.callback);
                }
            }
        }

        DreamEvent ev;
        ev.name = eventName;
        ev.data = data;
        ev.size = size;
        ev.timestamp = GetTickCount64();

        for (const auto& cb : callbacksToInvoke) {
            DreamCrashGuard::ExecuteSafely("EventBusHandler", [&]() {
                try {
                    cb(ev);
                } catch (...) {
                    DREAM_LOG_WARN("DreamEventBus: Exception caught in handler for event '%s'", eventName.c_str());
                }
            });
        }
        return callbacksToInvoke.size();
    }

    // Publish string event payload
    size_t PublishString(const std::string& eventName, const std::string& message) {
        return Publish(eventName, message.data(), message.size());
    }

    // Clear all handlers
    void Clear() {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_subscribers.clear();
    }

private:
    struct Subscriber {
        uint64_t id;
        DreamEventCallback callback;
    };

    DreamEventBus() = default;
    mutable std::mutex m_mutex;
    std::atomic<uint64_t> m_nextHandlerId{0};
    std::unordered_map<std::string, std::vector<Subscriber>> m_subscribers;
};

} // namespace dreamkurumi
