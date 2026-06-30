#include "graphics/GeometryRegistry.hpp"

#include "graphics/Buffer.hpp"
#include "graphics/Mesh.hpp" // for Vertex definition
#include "graphics/VulkanDevice.hpp"
#include "vk_mem_alloc.h"

#include <stdexcept>

namespace saida {

GeometryRegistry::GeometryRegistry(VulkanDevice& device, VkDeviceSize maxVertices, VkDeviceSize maxIndices)
    : device_(device) {
    
    VkDeviceSize vertexBufferSize = maxVertices * sizeof(Vertex);
    VkDeviceSize indexBufferSize = maxIndices * sizeof(uint32_t);

    // We use transfer_dst for copying data in, and vertex/index for drawing.
    // If BDA is enabled, we could also add SHADER_DEVICE_ADDRESS bit.
    vertexBuffer_ = std::make_unique<Buffer>(device_, vertexBufferSize,
        VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
        MemoryUsage::GpuOnly);

    indexBuffer_ = std::make_unique<Buffer>(device_, indexBufferSize,
        VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
        MemoryUsage::GpuOnly);

    VmaVirtualBlockCreateInfo vbInfo{};
    vbInfo.size = vertexBufferSize;
    if (vmaCreateVirtualBlock(&vbInfo, &vertexBlock_) != VK_SUCCESS)
        throw std::runtime_error("failed to create vertex virtual block");

    VmaVirtualBlockCreateInfo ibInfo{};
    ibInfo.size = indexBufferSize;
    if (vmaCreateVirtualBlock(&ibInfo, &indexBlock_) != VK_SUCCESS)
        throw std::runtime_error("failed to create index virtual block");
}

GeometryRegistry::~GeometryRegistry() {
    if (vertexBlock_) vmaDestroyVirtualBlock(vertexBlock_);
    if (indexBlock_) vmaDestroyVirtualBlock(indexBlock_);
}

GeometryAllocation GeometryRegistry::allocate(const std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices) {
    GeometryAllocation alloc{};
    alloc.indexCount = static_cast<uint32_t>(indices.size());

    VkDeviceSize vertexSize = vertices.size() * sizeof(Vertex);
    VkDeviceSize indexSize = indices.size() * sizeof(uint32_t);

    VmaVirtualAllocationCreateInfo allocInfo{};
    
    // Allocate vertex block
    allocInfo.size = vertexSize;
    allocInfo.alignment = 0; // Must be power of 2 or 0. sizeof(Vertex) is 68 (not a power of 2)!
    VkDeviceSize vertexOffsetBytes;
    if (vmaVirtualAllocate(vertexBlock_, &allocInfo, &alloc.vertexVirtualAlloc, &vertexOffsetBytes) != VK_SUCCESS)
        throw std::runtime_error("failed to allocate vertex space in GeometryRegistry");
    alloc.vertexOffset = static_cast<int32_t>(vertexOffsetBytes / sizeof(Vertex));

    // Allocate index block
    allocInfo.size = indexSize;
    allocInfo.alignment = sizeof(uint32_t);
    VkDeviceSize indexOffsetBytes;
    if (vmaVirtualAllocate(indexBlock_, &allocInfo, &alloc.indexVirtualAlloc, &indexOffsetBytes) != VK_SUCCESS) {
        vmaVirtualFree(vertexBlock_, alloc.vertexVirtualAlloc);
        throw std::runtime_error("failed to allocate index space in GeometryRegistry");
    }
    alloc.firstIndex = static_cast<uint32_t>(indexOffsetBytes / sizeof(uint32_t));

    // Copy data to GPU
    Buffer stagingVertex(device_, vertexSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, MemoryUsage::HostVisible);
    stagingVertex.write(vertices.data(), vertexSize);

    Buffer stagingIndex(device_, indexSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, MemoryUsage::HostVisible);
    stagingIndex.write(indices.data(), indexSize);

    device_.copyBuffer(stagingVertex.handle(), vertexBuffer_->handle(), vertexSize, 0, vertexOffsetBytes);
    device_.copyBuffer(stagingIndex.handle(), indexBuffer_->handle(), indexSize, 0, indexOffsetBytes);

    return alloc;
}

void GeometryRegistry::free(const GeometryAllocation& alloc) {
    if (alloc.vertexVirtualAlloc) {
        vmaVirtualFree(vertexBlock_, alloc.vertexVirtualAlloc);
    }
    if (alloc.indexVirtualAlloc) {
        vmaVirtualFree(indexBlock_, alloc.indexVirtualAlloc);
    }
}

} // namespace saida
