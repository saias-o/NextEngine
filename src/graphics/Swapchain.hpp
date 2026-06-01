#pragma once

#include "graphics/VmaFwd.hpp"

#include <vector>

namespace ne {

class VulkanDevice;
class Window;

// Owns the swap chain together with its depth buffer, render pass and
// framebuffers. The render pass is created once and survives recreation
// (so pipelines built against it stay valid); everything else is rebuilt
// on resize via recreate().
class Swapchain {
public:
    Swapchain(VulkanDevice& device, Window& window);
    ~Swapchain();
    Swapchain(const Swapchain&) = delete;
    Swapchain& operator=(const Swapchain&) = delete;

    // Waits until the window has a non-zero size, then rebuilds the swap chain.
    void recreate();

    VkSwapchainKHR handle() const { return swapchain_; }
    VkRenderPass renderPass() const { return renderPass_; }
    VkFramebuffer framebuffer(uint32_t index) const { return framebuffers_[index]; }
    VkExtent2D extent() const { return extent_; }
    VkSampleCountFlagBits samples() const { return samples_; }
    uint32_t imageCount() const { return static_cast<uint32_t>(images_.size()); }
    float aspectRatio() const { return extent_.width / static_cast<float>(extent_.height); }

    // Signalled when rendering to the given swap-chain image is done; presented
    // image is waited on by present. One per image (not per frame-in-flight) so
    // a semaphore is never reused while its present is still pending.
    VkSemaphore renderFinishedSemaphore(uint32_t imageIndex) const {
        return renderFinishedSemaphores_[imageIndex];
    }

private:
    void createSwapchain();
    void createImageViews();
    void createRenderPass();
    void createColorResources();
    void createDepthResources();
    void createFramebuffers();
    void createRenderFinishedSemaphores();
    void cleanup();

    VkSurfaceFormatKHR chooseSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& available) const;
    VkPresentModeKHR choosePresentMode(const std::vector<VkPresentModeKHR>& available) const;
    VkExtent2D chooseExtent(const VkSurfaceCapabilitiesKHR& capabilities) const;

    VulkanDevice& device_;
    Window& window_;

    VkSwapchainKHR swapchain_ = VK_NULL_HANDLE;
    std::vector<VkImage> images_;
    std::vector<VkImageView> imageViews_;
    VkFormat imageFormat_{};
    VkExtent2D extent_{};
    VkSampleCountFlagBits samples_ = VK_SAMPLE_COUNT_1_BIT;

    // Multisampled color target, resolved into the swap-chain image for present.
    VkImage colorImage_ = VK_NULL_HANDLE;
    VmaAllocation colorAllocation_ = VK_NULL_HANDLE;
    VkImageView colorImageView_ = VK_NULL_HANDLE;

    VkImage depthImage_ = VK_NULL_HANDLE;
    VmaAllocation depthAllocation_ = VK_NULL_HANDLE;
    VkImageView depthImageView_ = VK_NULL_HANDLE;

    VkRenderPass renderPass_ = VK_NULL_HANDLE;
    std::vector<VkFramebuffer> framebuffers_;
    std::vector<VkSemaphore> renderFinishedSemaphores_;  // one per swap-chain image
};

} // namespace ne
