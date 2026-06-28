#pragma once

#include "graphics/VmaFwd.hpp"

#include <vector>

namespace ne {

class VulkanDevice;
class Window;

// Swapchain plus MSAA color target and depth buffer. Dynamic rendering means
// the Renderer owns layout transitions and vkCmdBeginRendering.
class Swapchain {
public:
    Swapchain(VulkanDevice& device, Window& window);
    ~Swapchain();
    Swapchain(const Swapchain&) = delete;
    Swapchain& operator=(const Swapchain&) = delete;

    // Waits until the window has a non-zero size, then rebuilds the swap chain.
    void recreate();

    VkSwapchainKHR handle() const { return swapchain_; }
    VkExtent2D extent() const { return extent_; }
    VkSampleCountFlagBits samples() const { return samples_; }
    uint32_t imageCount() const { return static_cast<uint32_t>(images_.size()); }
    float aspectRatio() const { return extent_.width / static_cast<float>(extent_.height); }

    VkFormat colorFormat() const { return imageFormat_; }
    VkFormat depthFormat() const { return depthFormat_; }

    // Per-image present target (single-sample).
    VkImage image(uint32_t i) const { return images_[i]; }
    VkImageView imageView(uint32_t i) const { return imageViews_[i]; }

    // Multisampled color target (rendered into, then resolved to the swap image).
    // Null views/images when samples == 1 (render straight to the swap image).
    VkImage msaaColorImage() const { return colorImage_; }
    VkImageView msaaColorView() const { return colorImageView_; }

    VkImage depthImage() const { return depthImage_; }
    VkImageView depthView() const { return depthImageView_; }

    // Signalled when rendering to the given swap-chain image is done. One per
    // image (not per frame-in-flight) so a semaphore is never reused while its
    // present is still pending.
    VkSemaphore renderFinishedSemaphore(uint32_t imageIndex) const {
        return renderFinishedSemaphores_[imageIndex];
    }

private:
    void createSwapchain();
    void createImageViews();
    void createColorResources();
    void createDepthResources();
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
    VkFormat depthFormat_{};
    VkExtent2D extent_{};
    VkSampleCountFlagBits samples_ = VK_SAMPLE_COUNT_1_BIT;

    // Multisampled color target, resolved into the swap-chain image for present.
    VkImage colorImage_ = VK_NULL_HANDLE;
    VmaAllocation colorAllocation_ = VK_NULL_HANDLE;
    VkImageView colorImageView_ = VK_NULL_HANDLE;

    VkImage depthImage_ = VK_NULL_HANDLE;
    VmaAllocation depthAllocation_ = VK_NULL_HANDLE;
    VkImageView depthImageView_ = VK_NULL_HANDLE;

    std::vector<VkSemaphore> renderFinishedSemaphores_;  // one per swap-chain image
};

} // namespace ne
