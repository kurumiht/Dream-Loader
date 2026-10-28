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


// ============================================================================
// DreamShadowVTable (Shadow VMT Swap / Per-Instance Virtual Table Hooking)
// Creates an isolated copy of the virtual method table for a single object
// without touching read-only .rdata pages or affecting other instances!
// ============================================================================
class DreamShadowVTable {
public:
    DreamShadowVTable() = default;
    explicit DreamShadowVTable(void* instance, size_t methodCount = 64) {
        Initialize(instance, methodCount);
    }
    ~DreamShadowVTable() {
        UnhookAll();
    }

    bool Initialize(void* instance, size_t methodCount = 64) {
        if (!instance) return false;
        m_instance = instance;
        m_originalVtable = *reinterpret_cast<void***>(m_instance);
        if (!m_originalVtable) return false;

        m_methodCount = methodCount;
        m_shadowVtable.resize(m_methodCount);
        for (size_t i = 0; i < m_methodCount; ++i) {
            m_shadowVtable[i] = m_originalVtable[i];
        }

        // Swap instance vptr to point to shadow vtable
        *reinterpret_cast<void***>(m_instance) = m_shadowVtable.data();
        m_isHooked = true;
        DREAM_LOG_INFO("DreamShadowVTable: Swapped instance 0x%p to shadow VMT (size %zu)", m_instance, m_methodCount);
        return true;
    }

    bool Hook(size_t index, void* detour, void** ppOriginal = nullptr) {
        if (!m_isHooked || index >= m_methodCount || !detour) return false;

        if (ppOriginal) {
            *ppOriginal = m_originalVtable[index];
        }

        m_shadowVtable[index] = detour;
        DREAM_LOG_INFO("DreamShadowVTable: Hooked method index %zu to 0x%p", index, detour);
        return true;
    }

    bool Unhook(size_t index) {
        if (!m_isHooked || index >= m_methodCount) return false;
        m_shadowVtable[index] = m_originalVtable[index];
        return true;
    }

    void UnhookAll() {
        if (m_isHooked && m_instance && m_originalVtable) {
            *reinterpret_cast<void***>(m_instance) = m_originalVtable;
            m_isHooked = false;
            DREAM_LOG_INFO("DreamShadowVTable: Restored original VMT for instance 0x%p", m_instance);
        }
    }

    void* GetOriginal(size_t index) const {
        if (index < m_methodCount && m_originalVtable) {
            return m_originalVtable[index];
        }
        return nullptr;
    }

    bool IsHooked() const { return m_isHooked; }

private:
    void* m_instance{nullptr};
    void** m_originalVtable{nullptr};
    std::vector<void*> m_shadowVtable;
    size_t m_methodCount{0};
    bool m_isHooked{false};
};

} // namespace dreamkurumi
