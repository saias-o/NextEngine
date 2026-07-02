#include "graphics/StorageImage.hpp"

#include "graphics/VulkanDevice.hpp"

#include "vk_mem_alloc.h"

#include <stdexcept>

namespace saida {

StorageImage::StorageImage(VulkanDevice& device, uint32_t width, uint32_t height,
                           VkFormat format, VkImageUsageFlags usage)
    : device_(device), width_(width), height_(height), format_(format) {
    VkImageCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    info.imageType = VK_IMAGE_TYPE_2D;
    info.extent = {width_, height_, 1};
    info.mipLevels = 1;
    info.arrayLayers = 1;
    info.format = format_;
    info.tiling = VK_IMAGE_TILING_OPTIMAL;
    info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    info.usage = usage;
    info.samples = VK_SAMPLE_COUNT_1_BIT;
    info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo alloc{};
    alloc.usage = VMA_MEMORY_USAGE_AUTO;
    if (vmaCreateImage(device_.allocator(), &info, &alloc, &image_, &allocation_, nullptr) != VK_SUCCESS)
        throw std::runtime_error("failed to create storage image");

    view_ = device_.createImageView(image_, format_, VK_IMAGE_ASPECT_COLOR_BIT);
}

StorageImage::~StorageImage() {
    vkDestroyImageView(device_.device(), view_, nullptr);
    vmaDestroyImage(device_.allocator(), image_, allocation_);
}

} // namespace saida
