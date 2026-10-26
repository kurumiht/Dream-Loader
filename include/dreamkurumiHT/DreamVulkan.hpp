#pragma once
#include <windows.h>
#include <cstdint>
#include <functional>
#include <atomic>
#include <chrono>
#include "DreamHook.hpp"
#include "DreamLog.hpp"
#include "DreamPresets.hpp"
#include "DreamOverlay.hpp"
#include "DreamCrashGuard.hpp"

namespace dreamkurumi {

// ============================================================================
// Standard Vulkan WSI Definitions (Header-Only, Zero Vulkan SDK Dependency)
// Dissected for Vulkan 1.3 presentation pipeline in Enshrouded
// ============================================================================
#define VK_STRUCTURE_TYPE_PRESENT_INFO_KHR 1000001001

typedef void* VkQueue;
typedef int32_t VkResult;
typedef uint32_t VkStructureType;

struct VkPresentInfoKHR {
    VkStructureType sType;
    const void* pNext;
    uint32_t waitSemaphoreCount;
    const void* pWaitSemaphores;
    uint32_t swapchainCount;
    const void* pSwapchains;
    const uint32_t* pImageIndices;
    VkResult* pResults;
};

typedef VkResult (__stdcall *PFN_vkQueuePresentKHR)(VkQueue queue, const VkPresentInfoKHR* pPresentInfo);

// ============================================================================
// DreamVulkan - Native Vulkan Presentation Interceptor & In-Game GUI Hook
// Intercepts vkQueuePresentKHR to render in-game overlays (ImGui / Custom GUI)
// ============================================================================
class DreamVulkan {
public:
    using FramePresentCallback = std::function<void(VkQueue queue, const VkPresentInfoKHR* pPresentInfo)>;
    using OverlayRenderCallback = std::function<void()>;

    static DreamVulkan& Instance() {
        static DreamVulkan s_instance;
        return s_instance;
    }

    // Check if the Vulkan runtime DLL is loaded in current process
    bool IsVulkanLoaded() const {
        return GetModuleHandleA("vulkan-1.dll") != NULL;
    }

    void SetPresentCallback(FramePresentCallback cb) {
        m_presentCallback = cb;
    }

    void SetOverlayRenderCallback(OverlayRenderCallback cb) {
        m_overlayRenderCallback = cb;
    }

    uint64_t GetFrameCount() const { return m_frameCount.load(); }
    float GetCalculatedFps() const { return m_currentFps.load(); }

    // Install Vulkan queue presentation hook
    bool InstallHook() {
        if (m_hookInstalled.load()) return true;

        // Headless Dedicated Server protection: suppress graphics hook
        if (DreamServerGuard::IsDedicatedServer()) {
            DREAM_LOG_INFO("DreamVulkan: Headless dedicated server detected. Suppressing Vulkan present hook");
            return false;
        }

        HMODULE hVulkan = GetModuleHandleA("vulkan-1.dll");
        if (!hVulkan) {
            hVulkan = LoadLibraryA("vulkan-1.dll");
        }
        if (!hVulkan) {
            DREAM_LOG_WARN("DreamVulkan: vulkan-1.dll is not available in process");
            return false;
        }

        auto pfnPresent = reinterpret_cast<void*>(GetProcAddress(hVulkan, "vkQueuePresentKHR"));
        if (!pfnPresent) {
            DREAM_LOG_WARN("DreamVulkan: Failed to resolve exported vkQueuePresentKHR from vulkan-1.dll");
            return false;
        }

        if (!s_inlineHook.Install(pfnPresent, reinterpret_cast<void*>(HookedQueuePresent))) {
            DREAM_LOG_ERROR("DreamVulkan: Failed to install hook on vkQueuePresentKHR");
            return false;
        }

        s_originalQueuePresent = reinterpret_cast<PFN_vkQueuePresentKHR>(s_inlineHook.GetOriginal());
        m_hookInstalled.store(true);
        DREAM_LOG_INFO("DreamVulkan: vkQueuePresentKHR successfully hooked at %p (Original: %p)",
                       pfnPresent, s_originalQueuePresent);
        return true;
    }

    // Unhook Vulkan presentation cleanly
    void UninstallHook() {
        if (!m_hookInstalled.load()) return;
        s_inlineHook.Disable();
        m_hookInstalled.store(false);
        DREAM_LOG_INFO("DreamVulkan: vkQueuePresentKHR unhooked successfully");
    }

    // Called on each frame presentation
    void OnFramePresent(VkQueue queue, const VkPresentInfoKHR* pPresentInfo) {
        m_frameCount++;

        // Calculate real-time FPS
        auto now = std::chrono::steady_clock::now();
        auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(now - m_lastFpsCheck).count();
        if (elapsedMs >= 1000) {
            uint64_t framesThisSecond = m_frameCount.load() - m_lastFrameCount;
            m_currentFps.store(static_cast<float>(framesThisSecond * 1000.0f / elapsedMs));
            m_lastFrameCount = m_frameCount.load();
            m_lastFpsCheck = now;
        }

        // Call user frame callback safely under CrashGuard shield
        if (m_presentCallback) {
            DreamCrashGuard::ExecuteSafely("VulkanPresentCallback", [&]() {
                m_presentCallback(queue, pPresentInfo);
            });
        }

        // When in-game menu is toggled open via F9, trigger overlay render
        if (DreamOverlay::Instance().IsMenuOpen()) {
            if (m_overlayRenderCallback) {
                DreamCrashGuard::ExecuteSafely("VulkanOverlayRender", [&]() {
                    m_overlayRenderCallback();
                });
            }
        }
    }

private:
    DreamVulkan() {
        m_lastFpsCheck = std::chrono::steady_clock::now();
    }
    ~DreamVulkan() {
        UninstallHook();
    }

    static VkResult __stdcall HookedQueuePresent(VkQueue queue, const VkPresentInfoKHR* pPresentInfo) {
        DreamVulkan::Instance().OnFramePresent(queue, pPresentInfo);
        if (s_originalQueuePresent) {
            return s_originalQueuePresent(queue, pPresentInfo);
        }
        return 0; // VK_SUCCESS
    }

    static inline DreamInlineHook s_inlineHook;
    static inline PFN_vkQueuePresentKHR s_originalQueuePresent{nullptr};

    std::atomic<bool> m_hookInstalled{false};
    std::atomic<uint64_t> m_frameCount{0};
    std::atomic<float> m_currentFps{0.0f};
    uint64_t m_lastFrameCount{0};
    std::chrono::steady_clock::time_point m_lastFpsCheck;

    FramePresentCallback m_presentCallback;
    OverlayRenderCallback m_overlayRenderCallback;
};

} // namespace dreamkurumi
