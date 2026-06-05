#pragma once

#include "graphics/RenderCapabilities.hpp"
#include "graphics/VmaFwd.hpp"

#include <optional>
#include <vector>

namespace ne {

class Window;

struct QueueFamilyIndices {
    std::optional<uint32_t> graphicsFamily;
    std::optional<uint32_t> presentFamily;
    std::optional<uint32_t> computeFamily;          // a compute-capable family
    bool computeIsDedicated = false;                // compute family != graphics
    bool isComplete() const { return graphicsFamily.has_value() && presentFamily.has_value(); }
};

struct SwapChainSupportDetails {
    VkSurfaceCapabilitiesKHR capabilities{};
    std::vector<VkSurfaceFormatKHR> formats;
    std::vector<VkPresentModeKHR> presentModes;
};

// Owns the Vulkan instance, surface, physical/logical device, queues and a
// command pool. Acts as the central GPU handle that every other subsystem
// borrows by reference, and exposes the common allocation helpers.
class VulkanDevice {
public:
    explicit VulkanDevice(Window& window);
    ~VulkanDevice();
    VulkanDevice(const VulkanDevice&) = delete;
    VulkanDevice& operator=(const VulkanDevice&) = delete;

    VkInstance instance() const { return instance_; }
    VkDevice device() const { return device_; }
    VkPhysicalDevice physicalDevice() const { return physicalDevice_; }
    VkSurfaceKHR surface() const { return surface_; }
    VkQueue graphicsQueue() const { return graphicsQueue_; }
    VkQueue presentQueue() const { return presentQueue_; }
    // Async-compute queue. Falls back to the graphics queue when the GPU has no
    // dedicated compute family (caps.dedicatedComputeQueue tells which it is).
    VkQueue computeQueue() const { return computeQueue_; }
    uint32_t computeQueueFamily() const { return computeFamily_; }
    VkCommandPool commandPool() const { return commandPool_; }
    VkCommandPool computeCommandPool() const { return computeCommandPool_; }
    VmaAllocator allocator() const { return allocator_; }
    VkPipelineCache pipelineCache() const { return pipelineCache_; }
    VkSampleCountFlagBits maxUsableSampleCount() const;
    float maxAnisotropy() const;

    // What this GPU supports and which modern paths we enabled (single source of
    // truth for advanced-rendering feature gating).
    const RenderCapabilities& capabilities() const { return capabilities_; }

    QueueFamilyIndices findQueueFamilies() const { return findQueueFamilies(physicalDevice_); }
    SwapChainSupportDetails querySwapChainSupport() const { return querySwapChainSupport(physicalDevice_); }

    VkFormat findSupportedFormat(const std::vector<VkFormat>& candidates,
                                 VkImageTiling tiling, VkFormatFeatureFlags features) const;
    VkFormat findDepthFormat() const;

    void copyBuffer(VkBuffer src, VkBuffer dst, VkDeviceSize size) const;
    void copyBuffer(VkBuffer src, VkBuffer dst, VkDeviceSize size, VkDeviceSize srcOffset, VkDeviceSize dstOffset) const;
    VkImageView createImageView(VkImage image, VkFormat format, VkImageAspectFlags aspect) const;

    VkCommandBuffer beginSingleTimeCommands() const;
    void endSingleTimeCommands(VkCommandBuffer cmd) const;

private:
    void createInstance();
    void setupDebugMessenger();
    void createSurface(Window& window);
    void pickPhysicalDevice();
    void queryCapabilities();
    void createLogicalDevice();
    void createCommandPools();
    void createAllocator();
    void createPipelineCache();
    void savePipelineCache() const;

    bool checkValidationLayerSupport() const;
    std::vector<const char*> requiredInstanceExtensions() const;
    bool isDeviceSuitable(VkPhysicalDevice dev) const;
    bool checkDeviceExtensionSupport(VkPhysicalDevice dev) const;
    QueueFamilyIndices findQueueFamilies(VkPhysicalDevice dev) const;
    SwapChainSupportDetails querySwapChainSupport(VkPhysicalDevice dev) const;

    VkInstance instance_ = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT debugMessenger_ = VK_NULL_HANDLE;
    VkSurfaceKHR surface_ = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice_ = VK_NULL_HANDLE;
    VkDevice device_ = VK_NULL_HANDLE;
    VkQueue graphicsQueue_ = VK_NULL_HANDLE;
    VkQueue presentQueue_ = VK_NULL_HANDLE;
    VkQueue computeQueue_ = VK_NULL_HANDLE;
    uint32_t computeFamily_ = 0;
    VkCommandPool commandPool_ = VK_NULL_HANDLE;
    VkCommandPool computeCommandPool_ = VK_NULL_HANDLE;
    VmaAllocator allocator_ = VK_NULL_HANDLE;
    VkPipelineCache pipelineCache_ = VK_NULL_HANDLE;
    RenderCapabilities capabilities_;
    bool validationEnabled_ = false;
};

} // namespace ne
