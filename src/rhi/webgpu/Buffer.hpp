#pragma once

#include "rhi/BufferUsage.hpp"
#include "rhi/webgpu/WebGpu.hpp"

#include <cstdint>
#include <vector>

// WebGPU backend for rhi::Buffer (Étape 16.4). Same construction surface as the
// Vulkan Buffer (size, rhi::BufferUsage, MemoryUsage). HostVisible buffers keep
// a CPU shadow: mapped() returns it, write()/flush() push it to the GPU through
// queue.writeBuffer (persistent mapping does not exist on the web).

namespace saida::rhi::webgpu {

class Device;

// Mirrors saida::MemoryUsage (graphics/Buffer.hpp) so call sites read the same.
enum class MemoryUsage { GpuOnly, HostVisible };

class Buffer {
public:
    Buffer(Device& device, uint64_t size, rhi::BufferUsage usage, MemoryUsage memory);
    ~Buffer();
    Buffer(const Buffer&) = delete;
    Buffer& operator=(const Buffer&) = delete;

    WGPUBuffer handle() const { return buffer_; }
    uint64_t size() const { return size_; }

    // HostVisible only: CPU shadow pointer (upload with flush()).
    void* mapped() { return shadow_.empty() ? nullptr : shadow_.data(); }

    void write(const void* data, uint64_t size, uint64_t offset = 0);
    void flush(uint64_t size = 0);  // 0 = whole buffer

private:
    Device& device_;
    WGPUBuffer buffer_ = nullptr;
    uint64_t size_ = 0;
    std::vector<uint8_t> shadow_;  // HostVisible CPU copy
};

} // namespace saida::rhi::webgpu
