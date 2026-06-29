#pragma once

#include "graphics/VmaFwd.hpp"

namespace ne {

class VulkanDevice;

// Where a buffer's memory lives.
enum class MemoryUsage {
    GpuOnly,      // device-local, not host-accessible (vertex/index buffers)
    HostVisible,  // host-accessible and persistently mapped (staging/uniforms)
};

// RAII wrapper around a VkBuffer backed by a VMA allocation.
class Buffer {
public:
    Buffer(VulkanDevice& device, VkDeviceSize size, VkBufferUsageFlags usage, MemoryUsage memory);
    ~Buffer();
    Buffer(const Buffer&) = delete;
    Buffer& operator=(const Buffer&) = delete;

    // Copies `size` bytes into the (HostVisible) buffer and flushes it.
    void write(const void* data, VkDeviceSize size, VkDeviceSize offset = 0);
    void flush(VkDeviceSize size = VK_WHOLE_SIZE, VkDeviceSize offset = 0);
    VkBuffer handle() const { return buffer_; }
    VkDeviceSize size() const { return size_; }
    void* mapped() const { return mapped_; }

private:
    VulkanDevice& device_;
    VkBuffer buffer_ = VK_NULL_HANDLE;
    VmaAllocation allocation_ = VK_NULL_HANDLE;
    VkDeviceSize size_ = 0;
    void* mapped_ = nullptr;  // non-null for HostVisible buffers
    MemoryUsage memory_ = MemoryUsage::GpuOnly;
};

} // namespace ne
