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

// Local-space axis-aligned bounding box, in the mesh's own coordinate space
// (before any node transform). Computed once at construction; the physics layer
// uses it to auto-detect collider shapes without keeping a CPU vertex copy.
struct Aabb {
    glm::vec3 min{0.0f};
    glm::vec3 max{0.0f};

    glm::vec3 center() const { return (min + max) * 0.5f; }
    glm::vec3 extent() const { return max - min; }  // full size along each axis
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

    const Aabb& bounds() const { return bounds_; }

    // Lightweight CPU copy of the geometry (positions + indices only) kept for the
    // physics layer to build convex-hull / triangle-mesh colliders. (~12 B/vertex
    // + 4 B/index; could be made opt-in for shipping/mobile.)
    const std::vector<glm::vec3>& collisionVertices() const { return collisionVertices_; }
    const std::vector<uint32_t>& collisionIndices() const { return collisionIndices_; }

private:
    GeometryRegistry& registry_;
    GeometryAllocation allocation_;
    Aabb bounds_;
    std::vector<glm::vec3> collisionVertices_;
    std::vector<uint32_t> collisionIndices_;
};

} // namespace ne
