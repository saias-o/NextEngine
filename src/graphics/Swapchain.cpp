#include "graphics/Swapchain.hpp"

#include "core/Window.hpp"
#include "core/Log.hpp"
#include "graphics/VulkanDevice.hpp"
#include "vk_mem_alloc.h"

#include <algorithm>
#include <array>
#include <limits>
#include <stdexcept>

namespace ne {

namespace {
const char* presentModeName(VkPresentModeKHR mode) {
    switch (mode) {
    case VK_PRESENT_MODE_IMMEDIATE_KHR: return "IMMEDIATE";
    case VK_PRESENT_MODE_MAILBOX_KHR: return "MAILBOX";
    case VK_PRESENT_MODE_FIFO_KHR: return "FIFO";
    case VK_PRESENT_MODE_FIFO_RELAXED_KHR: return "FIFO_RELAXED";
    default: return "UNKNOWN";
    }
}

bool hasPresentMode(const std::vector<VkPresentModeKHR>& available, VkPresentModeKHR mode) {
    return std::find(available.begin(), available.end(), mode) != available.end();
}
}

Swapchain::Swapchain(VulkanDevice& device, Window& window, bool vSync)
    : device_(device), window_(window), vSync_(vSync) {
    samples_ = device_.maxUsableSampleCount();
    depthFormat_ = device_.findDepthFormat();
    createSwapchain();
    createImageViews();
    createColorResources();
    createDepthResources();
    createRenderFinishedSemaphores();
}

bool Swapchain::setVSync(bool enabled) {
    if (vSync_ == enabled) return false;
    vSync_ = enabled;
    recreate();
    return true;
}

Swapchain::~Swapchain() {
    cleanup();
}

void Swapchain::recreate() {
    int width = 0, height = 0;
    window_.framebufferSize(width, height);
    while (width == 0 || height == 0) {
        window_.framebufferSize(width, height);
        window_.waitEvents();
    }
    vkDeviceWaitIdle(device_.device());

    cleanup();
    createSwapchain();
    createImageViews();
    createColorResources();
    createDepthResources();
    createRenderFinishedSemaphores();
}

void Swapchain::cleanup() {
    for (auto sem : renderFinishedSemaphores_) vkDestroySemaphore(device_.device(), sem, nullptr);
    renderFinishedSemaphores_.clear();

    if (colorImageView_) vkDestroyImageView(device_.device(), colorImageView_, nullptr);
    if (colorImage_) vmaDestroyImage(device_.allocator(), colorImage_, colorAllocation_);
    colorImageView_ = VK_NULL_HANDLE;
    colorImage_ = VK_NULL_HANDLE;

    if (depthImageView_) vkDestroyImageView(device_.device(), depthImageView_, nullptr);
    if (depthImage_) vmaDestroyImage(device_.allocator(), depthImage_, depthAllocation_);
    depthImageView_ = VK_NULL_HANDLE;
    depthImage_ = VK_NULL_HANDLE;
    depthAllocation_ = VK_NULL_HANDLE;

    for (auto iv : imageViews_) vkDestroyImageView(device_.device(), iv, nullptr);
    imageViews_.clear();

    if (swapchain_) vkDestroySwapchainKHR(device_.device(), swapchain_, nullptr);
    swapchain_ = VK_NULL_HANDLE;
}

void Swapchain::createRenderFinishedSemaphores() {
    VkSemaphoreCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    renderFinishedSemaphores_.resize(images_.size());
    for (auto& sem : renderFinishedSemaphores_)
        if (vkCreateSemaphore(device_.device(), &ci, nullptr, &sem) != VK_SUCCESS)
            throw std::runtime_error("failed to create render-finished semaphore");
}

VkSurfaceFormatKHR Swapchain::chooseSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& available) const {
    for (auto& f : available)
        if (f.format == VK_FORMAT_B8G8R8A8_SRGB &&
            f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) return f;
    return available[0];
}

VkPresentModeKHR Swapchain::choosePresentMode(const std::vector<VkPresentModeKHR>& available) const {
    if (vSync_)
        return VK_PRESENT_MODE_FIFO_KHR;  // guaranteed by the Vulkan spec

    if (hasPresentMode(available, VK_PRESENT_MODE_IMMEDIATE_KHR))
        return VK_PRESENT_MODE_IMMEDIATE_KHR;
    if (hasPresentMode(available, VK_PRESENT_MODE_MAILBOX_KHR))
        return VK_PRESENT_MODE_MAILBOX_KHR;
    if (hasPresentMode(available, VK_PRESENT_MODE_FIFO_RELAXED_KHR))
        return VK_PRESENT_MODE_FIFO_RELAXED_KHR;
    return VK_PRESENT_MODE_FIFO_KHR;
}

VkExtent2D Swapchain::chooseExtent(const VkSurfaceCapabilitiesKHR& cap) const {
    if (cap.currentExtent.width != std::numeric_limits<uint32_t>::max())
        return cap.currentExtent;
    int w, h;
    window_.framebufferSize(w, h);
    VkExtent2D ext = {static_cast<uint32_t>(w), static_cast<uint32_t>(h)};
    ext.width = std::clamp(ext.width, cap.minImageExtent.width, cap.maxImageExtent.width);
    ext.height = std::clamp(ext.height, cap.minImageExtent.height, cap.maxImageExtent.height);
    return ext;
}

void Swapchain::createSwapchain() {
    auto support = device_.querySwapChainSupport();
    auto format = chooseSurfaceFormat(support.formats);
    auto mode = choosePresentMode(support.presentModes);
    auto extent = chooseExtent(support.capabilities);
    Log::info("Swapchain present mode: ", presentModeName(mode), " (vsync=", vSync_ ? "on" : "off", ")");

    uint32_t imageCount = support.capabilities.minImageCount + 1;
    if (support.capabilities.maxImageCount > 0 && imageCount > support.capabilities.maxImageCount)
        imageCount = support.capabilities.maxImageCount;

    VkSwapchainCreateInfoKHR ci{};
    ci.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    ci.surface = device_.surface();
    ci.minImageCount = imageCount;
    ci.imageFormat = format.format;
    ci.imageColorSpace = format.colorSpace;
    ci.imageExtent = extent;
    ci.imageArrayLayers = 1;
    ci.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

    auto idx = device_.findQueueFamilies();
    uint32_t queueFamilyIndices[] = {idx.graphicsFamily.value(), idx.presentFamily.value()};
    if (idx.graphicsFamily != idx.presentFamily) {
        ci.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
        ci.queueFamilyIndexCount = 2;
        ci.pQueueFamilyIndices = queueFamilyIndices;
    } else {
        ci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    }

    ci.preTransform = support.capabilities.currentTransform;
    ci.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    ci.presentMode = mode;
    ci.clipped = VK_TRUE;

    if (vkCreateSwapchainKHR(device_.device(), &ci, nullptr, &swapchain_) != VK_SUCCESS)
        throw std::runtime_error("failed to create swap chain");

    vkGetSwapchainImagesKHR(device_.device(), swapchain_, &imageCount, nullptr);
    images_.resize(imageCount);
    vkGetSwapchainImagesKHR(device_.device(), swapchain_, &imageCount, images_.data());
    imageFormat_ = format.format;
    extent_ = extent;
}

void Swapchain::createImageViews() {
    imageViews_.resize(images_.size());
    for (size_t i = 0; i < images_.size(); i++)
        imageViews_[i] = device_.createImageView(images_[i], imageFormat_, VK_IMAGE_ASPECT_COLOR_BIT);
}

void Swapchain::createColorResources() {
    // No separate MSAA target when single-sampled: render straight to the swap image.
    if (samples_ == VK_SAMPLE_COUNT_1_BIT) return;

    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent.width = extent_.width;
    imageInfo.extent.height = extent_.height;
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.format = imageFormat_;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.samples = samples_;

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
    allocInfo.flags = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT;
    allocInfo.priority = 1.0f;

    if (vmaCreateImage(device_.allocator(), &imageInfo, &allocInfo,
                       &colorImage_, &colorAllocation_, nullptr) != VK_SUCCESS)
        throw std::runtime_error("failed to create MSAA color image");

    colorImageView_ = device_.createImageView(colorImage_, imageFormat_, VK_IMAGE_ASPECT_COLOR_BIT);
}

void Swapchain::createDepthResources() {
    VkFormat depthFormat = device_.findDepthFormat();

    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent.width = extent_.width;
    imageInfo.extent.height = extent_.height;
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.format = depthFormat;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.samples = samples_;

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
    allocInfo.flags = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT;
    allocInfo.priority = 1.0f;

    if (vmaCreateImage(device_.allocator(), &imageInfo, &allocInfo,
                       &depthImage_, &depthAllocation_, nullptr) != VK_SUCCESS)
        throw std::runtime_error("failed to create depth image");

    depthImageView_ = device_.createImageView(depthImage_, depthFormat, VK_IMAGE_ASPECT_DEPTH_BIT);
}

} // namespace ne
