#pragma once
#include <windows.h>
#include <cstdint>
#include <cstddef>
#include <unordered_map>
#include "DreamCore.hpp"
#include "DreamLog.hpp"

namespace dreamkurumi {

// ============================================================================
// DreamVTableHook (Virtual Method Table Hooking Engine)
// ============================================================================
class DreamVTableHook {
public:
    DreamVTableHook() = default;
    explicit DreamVTableHook(void* instance) { Initialize(instance); }
    ~DreamVTableHook() { UnhookAll(); }

    bool Initialize(void* instance) {
        if (!instance) return false;
        m_instance = instance;
        m_vtable = *reinterpret_cast<void***>(m_instance);
        return m_vtable != nullptr;
    }

    bool Hook(size_t index, void* detour, void** ppOriginal = nullptr) {
        if (!m_vtable || !detour) return false;

        void** targetEntry = &m_vtable[index];

        // Save original if not already hooked
        if (m_originalEntries.find(index) == m_originalEntries.end()) {
            m_originalEntries[index] = *targetEntry;
        }

        if (ppOriginal) {
            *ppOriginal = m_originalEntries[index];
        }

        ScopedMemoryProtect protect(targetEntry, sizeof(void*), PAGE_EXECUTE_READWRITE);
        if (!protect.IsSuccess()) return false;

        *targetEntry = detour;
        DREAM_LOG_INFO("VTable index %zu hooked to 0x%p", index, detour);
        return true;
    }

    bool Unhook(size_t index) {
        auto it = m_originalEntries.find(index);
        if (it == m_originalEntries.end() || !m_vtable) return false;

        void** targetEntry = &m_vtable[index];
        ScopedMemoryProtect protect(targetEntry, sizeof(void*), PAGE_EXECUTE_READWRITE);
        if (!protect.IsSuccess()) return false;

        *targetEntry = it->second;
        m_originalEntries.erase(it);
        DREAM_LOG_INFO("VTable index %zu unhooked", index);
        return true;
    }

    void UnhookAll() {
        if (!m_vtable) return;
        for (const auto& [idx, orig] : m_originalEntries) {
            void** targetEntry = &m_vtable[idx];
            ScopedMemoryProtect protect(targetEntry, sizeof(void*), PAGE_EXECUTE_READWRITE);
            if (protect.IsSuccess()) {
                *targetEntry = orig;
            }
        }
        m_originalEntries.clear();
    }

    void* GetOriginal(size_t index) const {
        auto it = m_originalEntries.find(index);
        if (it != m_originalEntries.end()) {
            return it->second;
        }
        return m_vtable ? m_vtable[index] : nullptr;
    }

private:
    void* m_instance{nullptr};
    void** m_vtable{nullptr};
    std::unordered_map<size_t, void*> m_originalEntries;
};

} // namespace dreamkurumi
