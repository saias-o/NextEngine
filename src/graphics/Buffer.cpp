#include "graphics/Buffer.hpp"

#include "graphics/MemoryProfiler.hpp"
#include "graphics/VulkanDevice.hpp"
#include "vk_mem_alloc.h"

#include <cstring>
#include <stdexcept>

namespace saida {

Buffer::Buffer(VulkanDevice& device, VkDeviceSize size, VkBufferUsageFlags usage, MemoryUsage memory)
    : device_(device), size_(size), memory_(memory) {
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
    MemoryProfiler::registerAllocation(
        memory_ == MemoryUsage::HostVisible ? "Buffer/HostVisible" : "Buffer/GpuOnly",
        static_cast<uint64_t>(size_));
}

Buffer::~Buffer() {
    MemoryProfiler::unregisterAllocation(
        memory_ == MemoryUsage::HostVisible ? "Buffer/HostVisible" : "Buffer/GpuOnly",
        static_cast<uint64_t>(size_));
    vmaDestroyBuffer(device_.allocator(), buffer_, allocation_);
}

void Buffer::write(const void* data, VkDeviceSize size, VkDeviceSize offset) {
    if (!mapped_)
        throw std::runtime_error("write() called on a non host-visible buffer");
    memcpy(static_cast<uint8_t*>(mapped_) + offset, data, static_cast<size_t>(size));
    vmaFlushAllocation(device_.allocator(), allocation_, 0, size);
}

void Buffer::flush(VkDeviceSize size, VkDeviceSize offset) {
    if (!mapped_) return;
    vmaFlushAllocation(device_.allocator(), allocation_, offset, size);
}

} // namespace saida
