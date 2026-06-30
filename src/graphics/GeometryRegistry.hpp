#pragma once

#include "graphics/VmaFwd.hpp"
#include <vulkan/vulkan.h>
#include <memory>
#include <vector>

namespace saida {

class VulkanDevice;
class Buffer;
struct Vertex;

struct GeometryAllocation {
    VmaVirtualAllocation vertexVirtualAlloc = VK_NULL_HANDLE;
    VmaVirtualAllocation indexVirtualAlloc = VK_NULL_HANDLE;
    uint32_t firstIndex = 0;
    uint32_t indexCount = 0;
    int32_t vertexOffset = 0;
};

// Manages a single unified vertex buffer and index buffer, sub-allocating space
// for individual meshes using VMA's Virtual Blocks.
class GeometryRegistry {
public:
    static constexpr VkDeviceSize kDefaultMaxVertices = 1 * 1024 * 1024;
    static constexpr VkDeviceSize kDefaultMaxIndices = 3 * 1024 * 1024;

    // Default to ~64MB vertices (1M vertices) and ~12MB indices (3M indices).
    GeometryRegistry(VulkanDevice& device,
                     VkDeviceSize maxVertices = kDefaultMaxVertices,
                     VkDeviceSize maxIndices = kDefaultMaxIndices);
    ~GeometryRegistry();

    GeometryRegistry(const GeometryRegistry&) = delete;
    GeometryRegistry& operator=(const GeometryRegistry&) = delete;

    // Allocates space and uploads the geometry to the unified buffers.
    GeometryAllocation allocate(const std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices);

    // Frees a previously allocated geometry region.
    void free(const GeometryAllocation& alloc);

    Buffer* vertexBuffer() const { return vertexBuffer_.get(); }
    Buffer* indexBuffer() const { return indexBuffer_.get(); }

private:
    VulkanDevice& device_;
    std::unique_ptr<Buffer> vertexBuffer_;
    std::unique_ptr<Buffer> indexBuffer_;
    VmaVirtualBlock vertexBlock_ = VK_NULL_HANDLE;
    VmaVirtualBlock indexBlock_ = VK_NULL_HANDLE;
};

} // namespace saida
