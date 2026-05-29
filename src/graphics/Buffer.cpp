#include "graphics/Buffer.hpp"

#include "graphics/VulkanDevice.hpp"
#include "vk_mem_alloc.h"

#include <cstring>
#include <stdexcept>

namespace ne {

Buffer::Buffer(VulkanDevice& device, VkDeviceSize size, VkBufferUsageFlags usage, MemoryUsage memory)
    : device_(device), size_(size) {
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = size;
    bufferInfo.usage = usage;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
    if (memory == MemoryUsage::HostVisible) {
        allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT
                        | VMA_ALLOCATION_CREATE_MAPPED_BIT;
    }

    VmaAllocationInfo result{};
    if (vmaCreateBuffer(device_.allocator(), &bufferInfo, &allocInfo,
                        &buffer_, &allocation_, &result) != VK_SUCCESS)
        throw std::runtime_error("failed to create buffer");

    mapped_ = result.pMappedData;  // null when not host-visible
}

Buffer::~Buffer() {
    vmaDestroyBuffer(device_.allocator(), buffer_, allocation_);
}

void Buffer::write(const void* data, VkDeviceSize size) {
    if (!mapped_)
        throw std::runtime_error("write() called on a non host-visible buffer");
    memcpy(mapped_, data, static_cast<size_t>(size));
    vmaFlushAllocation(device_.allocator(), allocation_, 0, size);
}

} // namespace ne
