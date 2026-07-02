#pragma once

#include "graphics/VmaFwd.hpp"

#include <vulkan/vulkan.h>

namespace saida {

class VulkanDevice;

// GPU image usable as both compute storage and sampled texture.
class StorageImage {
public:
    StorageImage(VulkanDevice& device, uint32_t width, uint32_t height, VkFormat format,
                 VkImageUsageFlags usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
    ~StorageImage();
    StorageImage(const StorageImage&) = delete;
    StorageImage& operator=(const StorageImage&) = delete;

    VkImage image() const { return image_; }
    VkImageView view() const { return view_; }
    VkFormat format() const { return format_; }
    VkExtent2D extent() const { return {width_, height_}; }

private:
    VulkanDevice& device_;
    uint32_t width_, height_;
    VkFormat format_;
    VkImage image_ = VK_NULL_HANDLE;
    VmaAllocation allocation_ = VK_NULL_HANDLE;
    VkImageView view_ = VK_NULL_HANDLE;
};

} // namespace saida
