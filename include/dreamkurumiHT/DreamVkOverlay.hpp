#pragma once
// DreamVkOverlay.hpp - render Dear ImGui into the game's swapchain from a vkQueuePresentKHR hook.
//
// Synchronisation model (the part that usually goes wrong):
//   game render-done semaphores --wait--> [our submit: draw ImGui] --signal--> overlayDone[imageIndex]
//   vkQueuePresentKHR then waits on overlayDone[imageIndex] INSTEAD of the game's semaphores.
//   * our submit consumes the game's binary semaphores, so present must not wait on them again
//   * per-IMAGE signal semaphores (safe reuse), per-FRAME fence + command buffer (CPU/GPU pacing)
//   * the image is left in PRESENT_SRC_KHR by the game; our render pass goes PRESENT_SRC -> COLOR_ATTACHMENT
//     -> PRESENT_SRC with loadOp=LOAD, so no manual barriers and the game's picture is preserved.
//
// We submit on the SAME queue that is presenting, from inside the game's own vkQueuePresentKHR call, so we
// inherit whatever external synchronisation the game uses for that queue.
//
// Written against Dear ImGui 1.91.x (RenderPass inside ImGui_ImplVulkan_InitInfo, built WITHOUT
// IMGUI_IMPL_VULKAN_NO_PROTOTYPES, i.e. linked against vulkan-1). Version notes at ImguiInit().
//
// Hooks you must install and forward to the methods below (see glue at bottom):
//   vkCreateDevice, vkGetDeviceQueue (+ vkGetDeviceQueue2), vkCreateSwapchainKHR,
//   vkDestroySwapchainKHR, vkQueuePresentKHR

#include <windows.h>
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <mutex>
#include <vector>

#ifndef DREAM_VK_LOG
#define DREAM_VK_LOG(...) do { std::fprintf(stderr, "[DreamVkOverlay] " __VA_ARGS__); std::fputc('\n', stderr); } while (0)
#endif

#if __has_include(<vulkan/vulkan.h>) && __has_include(<imgui.h>) && __has_include(<imgui_impl_vulkan.h>)
#define DREAM_HAS_VULKAN_IMGUI 1
#include <vulkan/vulkan.h>
#include <imgui.h>
#include <imgui_impl_vulkan.h>
#else
#define DREAM_HAS_VULKAN_IMGUI 0
#endif

namespace dreamkurumi::vk {

#if DREAM_HAS_VULKAN_IMGUI

struct OverlayCallbacks {
    bool (*isVisible)()        = nullptr;  // your F1 toggle; when false the hook costs ~nothing
    void (*platformNewFrame)() = nullptr;  // ImGui_ImplWin32_NewFrame
    void (*buildUi)()          = nullptr;  // ImGui::Begin(...) ... ImGui::End()
};

class OverlayRenderer {
public:
    static OverlayRenderer& Instance() { static OverlayRenderer o; return o; }

    OverlayCallbacks cb;
    void SetEnabled(bool e) { m_enabled = e; }
    bool IsBackendAvailable() const { return true; }

    // ---- capture hooks ----------------------------------------------------------------------
    void OnDeviceCreated(VkPhysicalDevice phys, VkDevice dev) {
        std::lock_guard<std::mutex> g(m_mtx);
        m_devs.push_back({dev, phys});
    }
    void OnGetDeviceQueue(uint32_t family, VkQueue q) {   // from vkGetDeviceQueue / vkGetDeviceQueue2
        std::lock_guard<std::mutex> g(m_mtx);
        for (auto& e : m_queues) if (e.q == q) return;
        m_queues.push_back({q, family});
    }

    // ---- swapchain lifetime -----------------------------------------------------------------
    VkResult CreateSwapchain(PFN_vkCreateSwapchainKHR orig, VkDevice dev,
                             const VkSwapchainCreateInfoKHR* ci, const VkAllocationCallbacks* a,
                             VkSwapchainKHR* out) {
        // Need COLOR_ATTACHMENT to draw into the images (games that blit into the swapchain may omit it).
        VkSwapchainCreateInfoKHR c2 = *ci;
        c2.imageUsage |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        VkResult r = orig(dev, &c2, a, out);
        const VkSwapchainCreateInfoKHR* used = &c2;
        if (r != VK_SUCCESS) { r = orig(dev, ci, a, out); used = ci; }   // driver refused: fall back
        if (r != VK_SUCCESS) return r;

        std::lock_guard<std::mutex> g(m_mtx);
        TeardownLocked();                       // also covers the oldSwapchain->new swapchain case
        m_dev = dev;
        m_phys = VK_NULL_HANDLE;
        for (auto& d : m_devs) if (d.dev == dev) m_phys = d.phys;
        m_swapchain = *out;
        m_format = used->imageFormat;
        m_extent = used->imageExtent;
        m_hasColorUsage = (used->imageUsage & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT) != 0;
        m_dirty = true;
        return r;
    }

    void DestroySwapchain(PFN_vkDestroySwapchainKHR orig, VkDevice dev, VkSwapchainKHR sc,
                          const VkAllocationCallbacks* a) {
        {
            std::lock_guard<std::mutex> g(m_mtx);
            if (sc == m_swapchain) { TeardownLocked(); m_swapchain = VK_NULL_HANDLE; }
        }
        orig(dev, sc, a);
    }

    // ---- the actual overlay -----------------------------------------------------------------
    VkResult Present(PFN_vkQueuePresentKHR orig, VkQueue q, const VkPresentInfoKHR* pi) {
        std::unique_lock<std::mutex> lk(m_mtx, std::try_to_lock);
        if (!lk.owns_lock() || !m_enabled || !cb.isVisible || !cb.isVisible() ||
            pi->swapchainCount != 1 || pi->pSwapchains[0] != m_swapchain ||
            pi->waitSemaphoreCount > kMaxWait)
            return orig(q, pi);
        if (!EnsureReady(q) || q != m_queue) return orig(q, pi);

        const uint32_t img = pi->pImageIndices[0];
        if (img >= m_fbs.size()) return orig(q, pi);

        Frame& fr = m_frames[m_frameCounter % m_frames.size()];
        if (fr.pending) {                                   // previous use of this slot must be finished
            if (vkWaitForFences(m_dev, 1, &fr.fence, VK_TRUE, 100'000'000ull) != VK_SUCCESS)
                return orig(q, pi);                          // GPU behind: skip overlay this frame
            fr.pending = false;
        }

        // 1) build the UI (must be the thread that owns the ImGui context; this is the present thread)
        ImGui_ImplVulkan_NewFrame();
        if (cb.platformNewFrame) cb.platformNewFrame();
        ImGui::NewFrame();
        if (cb.buildUi) cb.buildUi();
        ImGui::Render();
        ImDrawData* dd = ImGui::GetDrawData();
        if (!dd || dd->TotalVtxCount == 0) return orig(q, pi);

        // 2) record
        vkResetCommandBuffer(fr.cmd, 0);
        VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        if (vkBeginCommandBuffer(fr.cmd, &bi) != VK_SUCCESS) return orig(q, pi);
        VkRenderPassBeginInfo rp{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
        rp.renderPass = m_rp;
        rp.framebuffer = m_fbs[img];
        rp.renderArea = {{0, 0}, m_extent};
        vkCmdBeginRenderPass(fr.cmd, &rp, VK_SUBPASS_CONTENTS_INLINE);
        ImGui_ImplVulkan_RenderDrawData(dd, fr.cmd);
        vkCmdEndRenderPass(fr.cmd);
        if (vkEndCommandBuffer(fr.cmd) != VK_SUCCESS) return orig(q, pi);

        // 3) submit: wait on the game's present-wait semaphores, signal ours
        VkPipelineStageFlags stages[kMaxWait];
        for (uint32_t i = 0; i < pi->waitSemaphoreCount; ++i) stages[i] = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        const VkSemaphore done = m_done[img];
        VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
        si.waitSemaphoreCount = pi->waitSemaphoreCount;
        si.pWaitSemaphores = pi->pWaitSemaphores;
        si.pWaitDstStageMask = stages;
        si.commandBufferCount = 1;
        si.pCommandBuffers = &fr.cmd;
        si.signalSemaphoreCount = 1;
        si.pSignalSemaphores = &done;
        vkResetFences(m_dev, 1, &fr.fence);
        if (vkQueueSubmit(q, 1, &si, fr.fence) != VK_SUCCESS) {
            // Game's semaphores were NOT consumed, so presenting normally is still valid. Stop trying.
            DREAM_VK_LOG("vkQueueSubmit failed; overlay disabled");
            m_enabled = false;
            return orig(q, pi);
        }
        fr.pending = true;
        ++m_frameCounter;

        // 4) present, waiting on OUR semaphore (which transitively waited on the game's)
        VkPresentInfoKHR p2 = *pi;                       // keeps pNext chain (present id, etc.)
        p2.waitSemaphoreCount = 1;
        p2.pWaitSemaphores = &done;                      // `done` is a local copy: safe after unlock
        lk.unlock();
        return orig(q, &p2);
    }

    // Call before DestroyContext / on shutdown.
    void Shutdown() { std::lock_guard<std::mutex> g(m_mtx); TeardownLocked(); }

private:
    static constexpr uint32_t kMaxWait = 8;
    struct DevInfo { VkDevice dev; VkPhysicalDevice phys; };
    struct QInfo   { VkQueue q; uint32_t family; };
    struct Frame   { VkCommandBuffer cmd = VK_NULL_HANDLE; VkFence fence = VK_NULL_HANDLE; bool pending = false; };

#define DREAM_VK_TRY(expr) do { if ((expr) != VK_SUCCESS) { DREAM_VK_LOG("%s failed", #expr); TeardownLocked(); return false; } } while (0)

    bool EnsureReady(VkQueue q) {
        if (!m_dirty) return m_ready;
        m_dirty = false;
        m_ready = false;
        if (!m_hasColorUsage || !m_phys) { DREAM_VK_LOG("swapchain unusable (no COLOR_ATTACHMENT / no physical device)"); return false; }

        uint32_t fam = UINT32_MAX;
        for (auto& e : m_queues) if (e.q == q) fam = e.family;
        if (fam == UINT32_MAX) { DREAM_VK_LOG("queue family unknown: install the vkGetDeviceQueue hook before the game creates queues"); return false; }
        uint32_t nProps = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(m_phys, &nProps, nullptr);
        std::vector<VkQueueFamilyProperties> props(nProps);
        vkGetPhysicalDeviceQueueFamilyProperties(m_phys, &nProps, props.data());
        if (fam >= nProps || !(props[fam].queueFlags & VK_QUEUE_GRAPHICS_BIT)) { DREAM_VK_LOG("present queue is not graphics-capable"); return false; }
        m_queue = q; m_family = fam;

        uint32_t n = 0;
        DREAM_VK_TRY(vkGetSwapchainImagesKHR(m_dev, m_swapchain, &n, nullptr));
        m_images.resize(n);
        DREAM_VK_TRY(vkGetSwapchainImagesKHR(m_dev, m_swapchain, &n, m_images.data()));

        // render pass: PRESENT_SRC -> COLOR_ATTACHMENT -> PRESENT_SRC, keep existing pixels
        VkAttachmentDescription att{};
        att.format = m_format;
        att.samples = VK_SAMPLE_COUNT_1_BIT;
        att.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
        att.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        att.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        att.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        att.initialLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        att.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        VkAttachmentReference ref{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
        VkSubpassDescription sub{};
        sub.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        sub.colorAttachmentCount = 1;
        sub.pColorAttachments = &ref;
        VkSubpassDependency deps[2]{};
        deps[0].srcSubpass = VK_SUBPASS_EXTERNAL; deps[0].dstSubpass = 0;
        deps[0].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        deps[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        deps[0].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        deps[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        deps[1].srcSubpass = 0; deps[1].dstSubpass = VK_SUBPASS_EXTERNAL;
        deps[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        deps[1].dstStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
        deps[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        deps[1].dstAccessMask = 0;
        VkRenderPassCreateInfo rpci{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
        rpci.attachmentCount = 1; rpci.pAttachments = &att;
        rpci.subpassCount = 1;    rpci.pSubpasses = &sub;
        rpci.dependencyCount = 2; rpci.pDependencies = deps;
        DREAM_VK_TRY(vkCreateRenderPass(m_dev, &rpci, nullptr, &m_rp));

        // per-image: view, framebuffer, "overlay done" semaphore
        m_views.assign(n, VK_NULL_HANDLE);
        m_fbs.assign(n, VK_NULL_HANDLE);
        m_done.assign(n, VK_NULL_HANDLE);
        for (uint32_t i = 0; i < n; ++i) {
            VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
            vi.image = m_images[i];
            vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
            vi.format = m_format;
            vi.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
            DREAM_VK_TRY(vkCreateImageView(m_dev, &vi, nullptr, &m_views[i]));
            VkFramebufferCreateInfo fi{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
            fi.renderPass = m_rp; fi.attachmentCount = 1; fi.pAttachments = &m_views[i];
            fi.width = m_extent.width; fi.height = m_extent.height; fi.layers = 1;
            DREAM_VK_TRY(vkCreateFramebuffer(m_dev, &fi, nullptr, &m_fbs[i]));
            VkSemaphoreCreateInfo sci{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
            DREAM_VK_TRY(vkCreateSemaphore(m_dev, &sci, nullptr, &m_done[i]));
        }

        // per-frame-in-flight: command buffer + fence (ring size == image count, matches ImGui's own buffer ring)
        VkCommandPoolCreateInfo pci{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
        pci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        pci.queueFamilyIndex = m_family;
        DREAM_VK_TRY(vkCreateCommandPool(m_dev, &pci, nullptr, &m_pool));
        const uint32_t nFrames = std::max<uint32_t>(n, 2);
        m_frames.assign(nFrames, Frame{});
        std::vector<VkCommandBuffer> cmds(nFrames);
        VkCommandBufferAllocateInfo ai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        ai.commandPool = m_pool; ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY; ai.commandBufferCount = nFrames;
        DREAM_VK_TRY(vkAllocateCommandBuffers(m_dev, &ai, cmds.data()));
        for (uint32_t i = 0; i < nFrames; ++i) {
            m_frames[i].cmd = cmds[i];
            VkFenceCreateInfo fci{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
            DREAM_VK_TRY(vkCreateFence(m_dev, &fci, nullptr, &m_frames[i].fence));
        }
        m_frameCounter = 0;

        // descriptor pool for ImGui textures (font atlas + any user textures)
        VkDescriptorPoolSize ps{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 32};
        VkDescriptorPoolCreateInfo dpi{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
        dpi.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
        dpi.maxSets = 32; dpi.poolSizeCount = 1; dpi.pPoolSizes = &ps;
        DREAM_VK_TRY(vkCreateDescriptorPool(m_dev, &dpi, nullptr, &m_descPool));

        if (!ImguiInit(n)) { DREAM_VK_LOG("ImGui_ImplVulkan_Init failed"); TeardownLocked(); return false; }
        m_ready = true;
        return true;
    }

    bool ImguiInit(uint32_t imageCount) {
        ImGui_ImplVulkan_InitInfo ii{};
        ii.PhysicalDevice = m_phys;
        ii.Device = m_dev;
        ii.QueueFamily = m_family;
        ii.Queue = m_queue;
        ii.DescriptorPool = m_descPool;
        ii.MinImageCount = std::max<uint32_t>(2, imageCount);
        ii.ImageCount = imageCount;
        ii.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
        ii.RenderPass = m_rp;
        ii.Subpass = 0;
        if (!ImGui_ImplVulkan_Init(&ii)) return false;
        m_backendInit = true;
        return true;
    }

    // Waits for OUR submissions only, then frees everything. Never waits on the game's work.
    void TeardownLocked() {
        m_ready = false;
        if (!m_dev) return;
        for (auto& f : m_frames)
            if (f.pending && f.fence) { vkWaitForFences(m_dev, 1, &f.fence, VK_TRUE, 2'000'000'000ull); f.pending = false; }
        if (m_backendInit) { ImGui_ImplVulkan_Shutdown(); m_backendInit = false; }
        for (auto s : m_done) if (s) vkDestroySemaphore(m_dev, s, nullptr);
        for (auto f : m_fbs)  if (f) vkDestroyFramebuffer(m_dev, f, nullptr);
        for (auto v : m_views) if (v) vkDestroyImageView(m_dev, v, nullptr);
        for (auto& f : m_frames) if (f.fence) vkDestroyFence(m_dev, f.fence, nullptr);
        if (m_pool)     { vkDestroyCommandPool(m_dev, m_pool, nullptr); m_pool = VK_NULL_HANDLE; }
        if (m_descPool) { vkDestroyDescriptorPool(m_dev, m_descPool, nullptr); m_descPool = VK_NULL_HANDLE; }
        if (m_rp)       { vkDestroyRenderPass(m_dev, m_rp, nullptr); m_rp = VK_NULL_HANDLE; }
        m_done.clear(); m_fbs.clear(); m_views.clear(); m_frames.clear(); m_images.clear();
    }
#undef DREAM_VK_TRY

    std::mutex m_mtx;
    std::atomic<bool> m_enabled{true};
    std::vector<DevInfo> m_devs;
    std::vector<QInfo>   m_queues;

    VkDevice m_dev = VK_NULL_HANDLE;
    VkPhysicalDevice m_phys = VK_NULL_HANDLE;
    VkQueue m_queue = VK_NULL_HANDLE;
    uint32_t m_family = 0;
    VkSwapchainKHR m_swapchain = VK_NULL_HANDLE;
    VkFormat m_format = VK_FORMAT_UNDEFINED;
    VkExtent2D m_extent{};
    bool m_hasColorUsage = false, m_dirty = false, m_ready = false, m_backendInit = false;

    std::vector<VkImage> m_images;
    std::vector<VkImageView> m_views;
    std::vector<VkFramebuffer> m_fbs;
    std::vector<VkSemaphore> m_done;
    std::vector<Frame> m_frames;
    uint64_t m_frameCounter = 0;
    VkRenderPass m_rp = VK_NULL_HANDLE;
    VkCommandPool m_pool = VK_NULL_HANDLE;
    VkDescriptorPool m_descPool = VK_NULL_HANDLE;
};

#else

// Stubs for build environments without vendored Dear ImGui & Vulkan SDK
struct OverlayCallbacks {
    bool (*isVisible)()        = nullptr;
    void (*platformNewFrame)() = nullptr;
    void (*buildUi)()          = nullptr;
};

class OverlayRenderer {
public:
    static OverlayRenderer& Instance() { static OverlayRenderer o; return o; }
    OverlayCallbacks cb;
    void SetEnabled(bool e) { m_enabled = e; }
    bool IsBackendAvailable() const { return false; }
    void Shutdown() {}
private:
    std::atomic<bool> m_enabled{false};
};

#endif

} // namespace dreamkurumi::vk

namespace dream {
    namespace vk = dreamkurumi::vk;
}
