#pragma once

#include <vulkan/vulkan.h>

namespace ne::xr {

class Instance;

// The OpenXR↔Vulkan bridge (XR_KHR_vulkan_enable2), as free functions taking the
// runtime Instance. This is what VulkanDevice calls in XR mode instead of the
// plain vkCreate*: the runtime merges its required extensions/features and binds
// the GPU the headset is on. Each throws on failure (XR or VK).
//
// `ci` is the engine's own VkInstanceCreateInfo / VkDeviceCreateInfo (app info,
// queues, extensions, features); the runtime augments it. No surface is involved
// — XR presents through its own swapchains, not a VkSurfaceKHR.

VkInstance createVulkanInstance(const Instance& xr, const VkInstanceCreateInfo& ci);
VkPhysicalDevice pickPhysicalDevice(const Instance& xr, VkInstance vulkanInstance);
VkDevice createVulkanDevice(const Instance& xr, VkPhysicalDevice physicalDevice,
                            const VkDeviceCreateInfo& ci);

} // namespace ne::xr
