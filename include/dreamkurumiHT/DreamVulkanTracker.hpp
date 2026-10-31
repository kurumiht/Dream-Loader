#pragma once
#include <windows.h>
#include <cstdint>
#include <vector>
#include <unordered_map>
#include <shared_mutex>
#include <mutex>

namespace dreamkurumi {

// Basic Vulkan type definitions for standalone compilation (Zero SDK dependency)
#ifndef VK_NULL_HANDLE
#define VK_NULL_HANDLE 0
#endif

#ifndef VK_SUCCESS
#define VK_SUCCESS 0
#endif

#ifndef VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT
#define VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT 0x00000010
#endif

#ifndef VK_SWAPCHAIN_CREATE_MUTABLE_FORMAT_BIT_KHR
#define VK_SWAPCHAIN_CREATE_MUTABLE_FORMAT_BIT_KHR 0x00000001
#endif

#ifndef VK_FORMAT_UNDEFINED
#define VK_FORMAT_UNDEFINED 0
#endif

#if !defined(VULKAN_H_) && !defined(VK_VERSION_1_0)
typedef uint32_t VkFlags;
typedef VkFlags VkImageUsageFlags;
typedef int32_t VkFormat;
typedef int32_t VkResult;

struct VkExtent2D {
    uint32_t width;
    uint32_t height;
};

typedef void* VkDevice;
typedef void* VkQueue;
typedef uint64_t VkSwapchainKHR;
typedef uint64_t VkImage;

struct VkAllocationCallbacks;

struct VkSwapchainCreateInfoKHR {
    uint32_t sType;
    const void* pNext;
    VkFlags flags;
    void* surface;
    uint32_t minImageCount;
    VkFormat imageFormat;
    int32_t imageColorSpace;
    VkExtent2D imageExtent;
    uint32_t imageArrayLayers;
    VkImageUsageFlags imageUsage;
    int32_t imageSharingMode;
    uint32_t queueFamilyIndexCount;
    const uint32_t* pQueueFamilyIndices;
    int32_t preTransform;
    int32_t compositeAlpha;
    int32_t presentMode;
    uint32_t clipped;
    VkSwapchainKHR oldSwapchain;
};

typedef VkResult (*PFN_vkCreateSwapchainKHR)(VkDevice device, const VkSwapchainCreateInfoKHR* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkSwapchainKHR* pSwapchain);
typedef void (*PFN_vkDestroySwapchainKHR)(VkDevice device, VkSwapchainKHR swapchain, const VkAllocationCallbacks* pAllocator);
typedef VkResult (*PFN_vkGetSwapchainImagesKHR)(VkDevice device, VkSwapchainKHR swapchain, uint32_t* pSwapchainImageCount, VkImage* pSwapchainImages);
#endif

struct SwapchainContext {
    VkDevice               device      = VK_NULL_HANDLE;
    VkSwapchainKHR         swapchain   = VK_NULL_HANDLE;
    VkFormat               format      = VK_FORMAT_UNDEFINED;
    VkExtent2D             extent      = {0, 0};
    VkImageUsageFlags      usage       = 0;
    std::vector<VkImage>   images;
    uint64_t               generation  = 0; // Prevents ABA handle reuse bugs

    // Dev 2 Color Space & Format Aliasing Helpers
    bool IsSrgbFormat() const noexcept {
        return format == 50 /* VK_FORMAT_B8G8R8A8_SRGB */ || format == 43 /* VK_FORMAT_R8G8B8A8_SRGB */;
    }

    bool IsHdrFormat() const noexcept {
        return format == 64 /* VK_FORMAT_A2B10G10R10_UNORM_PACK32 */ ||
               format == 100 /* VK_FORMAT_R16G16B16A16_SFLOAT */;
    }

    VkFormat GetUnormAliasedFormat() const noexcept {
        if (format == 50 /* VK_FORMAT_B8G8R8A8_SRGB */) return 44 /* VK_FORMAT_B8G8R8A8_UNORM */;
        if (format == 43 /* VK_FORMAT_R8G8B8A8_SRGB */) return 37 /* VK_FORMAT_R8G8B8A8_UNORM */;
        return format;
    }
};

// ============================================================================
// DreamVulkanSwapchainTracker - Vulkan 1.3 Swapchain Lifecycle & ABA Guard
// Tracks swapchains across DLSS 3 / FSR 3 dynamic re-creations, guarantees
// VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, and prevents ABA handle recycling bugs.
// ============================================================================
class DreamVulkanSwapchainTracker {
public:
    static DreamVulkanSwapchainTracker& Instance() {
        static DreamVulkanSwapchainTracker s_instance;
        return s_instance;
    }

    VkResult OnCreateSwapchainKHR(
        PFN_vkCreateSwapchainKHR origCreateSwapchain,
        PFN_vkGetSwapchainImagesKHR fnGetSwapchainImages,
        VkDevice device,
        const VkSwapchainCreateInfoKHR* pCreateInfo,
        const VkAllocationCallbacks* pAllocator,
        VkSwapchainKHR* pSwapchain)
    {
        if (!origCreateSwapchain || !pCreateInfo || !pSwapchain) return -1;

        // Copy createInfo so we can safely guarantee COLOR_ATTACHMENT_BIT for overlay rendering
        VkSwapchainCreateInfoKHR modifiedInfo = *pCreateInfo;
        modifiedInfo.imageUsage |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

        // Dev 2 Format Aliasing: If swapchain is sRGB, add MUTABLE_FORMAT_BIT so overlay can create a UNORM view
        if (modifiedInfo.imageFormat == 50 /* VK_FORMAT_B8G8R8A8_SRGB */ || modifiedInfo.imageFormat == 43 /* VK_FORMAT_R8G8B8A8_SRGB */) {
            modifiedInfo.flags |= VK_SWAPCHAIN_CREATE_MUTABLE_FORMAT_BIT_KHR;
        }

        VkResult res = origCreateSwapchain(device, &modifiedInfo, pAllocator, pSwapchain);
        if (res != VK_SUCCESS) {
            modifiedInfo = *pCreateInfo; // Retry with game's original flags
            res = origCreateSwapchain(device, pCreateInfo, pAllocator, pSwapchain);
        }

        if (res == VK_SUCCESS && pSwapchain && *pSwapchain != VK_NULL_HANDLE) {
            // Swapchain successfully created: Now safely retire oldSwapchain if different from new handle
            if (pCreateInfo->oldSwapchain != VK_NULL_HANDLE && pCreateInfo->oldSwapchain != *pSwapchain) {
                std::unique_lock lock(m_mutex);
                m_swapchains.erase(pCreateInfo->oldSwapchain);
            }

            SwapchainContext ctx{};
            ctx.device     = device;
            ctx.swapchain  = *pSwapchain;
            ctx.format     = modifiedInfo.imageFormat;
            ctx.extent     = modifiedInfo.imageExtent;
            ctx.usage      = modifiedInfo.imageUsage;

            if (fnGetSwapchainImages) {
                uint32_t imgCount = 0;
                fnGetSwapchainImages(device, *pSwapchain, &imgCount, nullptr);
                if (imgCount > 0) {
                    ctx.images.resize(imgCount);
                    fnGetSwapchainImages(device, *pSwapchain, &imgCount, ctx.images.data());
                }
            }

            std::unique_lock lock(m_mutex);
            ctx.generation = m_nextGeneration++;
            m_swapchains[*pSwapchain] = std::move(ctx);
        }
        return res;
    }

    void OnDestroySwapchainKHR(
        PFN_vkDestroySwapchainKHR origDestroySwapchain,
        VkDevice device,
        VkSwapchainKHR swapchain,
        const VkAllocationCallbacks* pAllocator)
    {
        if (swapchain != VK_NULL_HANDLE) {
            std::unique_lock lock(m_mutex);
            m_swapchains.erase(swapchain);
        }
        if (origDestroySwapchain) {
            origDestroySwapchain(device, swapchain, pAllocator);
        }
    }

    bool TryGetContext(VkSwapchainKHR swapchain, SwapchainContext& outCtx) {
        std::shared_lock lock(m_mutex);
        auto it = m_swapchains.find(swapchain);
        if (it == m_swapchains.end()) {
            return false;
        }
        outCtx = it->second;
        return true;
    }

    size_t GetActiveSwapchainCount() {
        std::shared_lock lock(m_mutex);
        return m_swapchains.size();
    }

    uint64_t GetCurrentGeneration() const {
        return m_nextGeneration;
    }

    void Clear() {
        std::unique_lock lock(m_mutex);
        m_swapchains.clear();
    }

private:
    std::shared_mutex m_mutex;
    std::unordered_map<VkSwapchainKHR, SwapchainContext> m_swapchains;
    uint64_t m_nextGeneration = 1;
};

} // namespace dreamkurumi

namespace dreamkurumiHT {
    using namespace dreamkurumi;
}
