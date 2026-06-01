#include "graphics/Swapchain.hpp"

#include "core/Window.hpp"
#include "graphics/VulkanDevice.hpp"
#include "vk_mem_alloc.h"

#include <algorithm>
#include <array>
#include <limits>
#include <stdexcept>

namespace ne {

Swapchain::Swapchain(VulkanDevice& device, Window& window)
    : device_(device), window_(window) {
    createSwapchain();
    createImageViews();
    createRenderPass();
    createDepthResources();
    createFramebuffers();
    createRenderFinishedSemaphores();
}

Swapchain::~Swapchain() {
    cleanup();
    vkDestroyRenderPass(device_.device(), renderPass_, nullptr);
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
    createDepthResources();
    createFramebuffers();
    createRenderFinishedSemaphores();
}

void Swapchain::cleanup() {
    for (auto sem : renderFinishedSemaphores_) vkDestroySemaphore(device_.device(), sem, nullptr);
    vkDestroyImageView(device_.device(), depthImageView_, nullptr);
    vmaDestroyImage(device_.allocator(), depthImage_, depthAllocation_);
    for (auto fb : framebuffers_) vkDestroyFramebuffer(device_.device(), fb, nullptr);
    for (auto iv : imageViews_) vkDestroyImageView(device_.device(), iv, nullptr);
    vkDestroySwapchainKHR(device_.device(), swapchain_, nullptr);
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
    for (auto& m : available)
        if (m == VK_PRESENT_MODE_MAILBOX_KHR) return m;
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

void Swapchain::createRenderPass() {
    VkAttachmentDescription colorAttach{};
    colorAttach.format = imageFormat_;
    colorAttach.samples = VK_SAMPLE_COUNT_1_BIT;
    colorAttach.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttach.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttach.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttach.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAttach.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    colorAttach.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentDescription depthAttach{};
    depthAttach.format = device_.findDepthFormat();
    depthAttach.samples = VK_SAMPLE_COUNT_1_BIT;
    depthAttach.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthAttach.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttach.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depthAttach.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttach.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    depthAttach.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkAttachmentReference colorRef{};
    colorRef.attachment = 0;
    colorRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkAttachmentReference depthRef{};
    depthRef.attachment = 1;
    depthRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorRef;
    subpass.pDepthStencilAttachment = &depthRef;

    VkSubpassDependency dep{};
    dep.srcSubpass = VK_SUBPASS_EXTERNAL;
    dep.dstSubpass = 0;
    dep.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT
                     | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dep.srcAccessMask = 0;
    dep.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT
                     | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT
                      | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

    std::array<VkAttachmentDescription, 2> attachments = {colorAttach, depthAttach};

    VkRenderPassCreateInfo rpci{};
    rpci.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    rpci.attachmentCount = static_cast<uint32_t>(attachments.size());
    rpci.pAttachments = attachments.data();
    rpci.subpassCount = 1;
    rpci.pSubpasses = &subpass;
    rpci.dependencyCount = 1;
    rpci.pDependencies = &dep;

    if (vkCreateRenderPass(device_.device(), &rpci, nullptr, &renderPass_) != VK_SUCCESS)
        throw std::runtime_error("failed to create render pass");
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
    imageInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
    allocInfo.flags = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT;
    allocInfo.priority = 1.0f;

    if (vmaCreateImage(device_.allocator(), &imageInfo, &allocInfo,
                       &depthImage_, &depthAllocation_, nullptr) != VK_SUCCESS)
        throw std::runtime_error("failed to create depth image");

    depthImageView_ = device_.createImageView(depthImage_, depthFormat, VK_IMAGE_ASPECT_DEPTH_BIT);
}

void Swapchain::createFramebuffers() {
    framebuffers_.resize(imageViews_.size());
    for (size_t i = 0; i < imageViews_.size(); i++) {
        std::array<VkImageView, 2> attachments = {imageViews_[i], depthImageView_};

        VkFramebufferCreateInfo ci{};
        ci.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        ci.renderPass = renderPass_;
        ci.attachmentCount = static_cast<uint32_t>(attachments.size());
        ci.pAttachments = attachments.data();
        ci.width = extent_.width;
        ci.height = extent_.height;
        ci.layers = 1;

        if (vkCreateFramebuffer(device_.device(), &ci, nullptr, &framebuffers_[i]) != VK_SUCCESS)
            throw std::runtime_error("failed to create framebuffer");
    }
}

} // namespace ne
