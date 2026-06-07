#pragma once

#include <vulkan/vulkan.h>
#include <glm/glm.hpp>

#include <array>
#include <memory>
#include <string>
#include <vector>

#include "graphics/GeometryRegistry.hpp"

namespace ne {

class VulkanDevice;
class Buffer;
class GeometryRegistry;

struct Vertex {
    glm::vec3 pos;
    glm::vec3 normal;
    glm::vec3 color;
    glm::vec2 texCoord;
    glm::vec2 lightmapUV;  // second, non-overlapping UV set for baked lightmaps
    glm::vec4 tangent;
    glm::ivec4 boneIndices{-1, -1, -1, -1};
    glm::vec4 boneWeights{0.0f, 0.0f, 0.0f, 0.0f};

    static VkVertexInputBindingDescription bindingDescription();
    static std::array<VkVertexInputAttributeDescription, 8> attributeDescriptions();
};

// Owns device-local vertex and index buffers and knows how to bind/draw itself.
class Mesh {
public:
    Mesh(GeometryRegistry& registry, const std::vector<Vertex>& vertices,
         const std::vector<uint32_t>& indices);
    ~Mesh();
    Mesh(const Mesh&) = delete;
    Mesh& operator=(const Mesh&) = delete;

    static std::unique_ptr<Mesh> fromObjFile(GeometryRegistry& registry, const std::string& path);

    void bind(VkCommandBuffer cmd) const;
    void draw(VkCommandBuffer cmd) const;

    GeometryAllocation geometryAllocation() const { return allocation_; }
    const GeometryAllocation& allocation() const { return allocation_; }

private:
    GeometryRegistry& registry_;
    GeometryAllocation allocation_;
};

} // namespace ne
