#pragma once

#include <vulkan/vulkan.h>
#include <glm/glm.hpp>

#include <array>
#include <memory>
#include <string>
#include <vector>

namespace ne {

class VulkanDevice;
class Buffer;

struct Vertex {
    glm::vec3 pos;
    glm::vec3 normal;
    glm::vec3 color;
    glm::vec2 texCoord;

    static VkVertexInputBindingDescription bindingDescription();
    static std::array<VkVertexInputAttributeDescription, 4> attributeDescriptions();
};

// Owns device-local vertex and index buffers and knows how to bind/draw itself.
class Mesh {
public:
    Mesh(VulkanDevice& device, const std::vector<Vertex>& vertices,
         const std::vector<uint32_t>& indices);
    ~Mesh();
    Mesh(const Mesh&) = delete;
    Mesh& operator=(const Mesh&) = delete;

    // Loads a Wavefront .obj (triangulated, recentered and uniformly scaled to
    // fit a unit cube). Vertex color is derived from the surface normal.
    static std::unique_ptr<Mesh> fromObjFile(VulkanDevice& device, const std::string& path);

    void bind(VkCommandBuffer cmd) const;
    void draw(VkCommandBuffer cmd) const;

private:
    void createVertexBuffer(const std::vector<Vertex>& vertices);
    void createIndexBuffer(const std::vector<uint32_t>& indices);

    VulkanDevice& device_;
    std::unique_ptr<Buffer> vertexBuffer_;
    std::unique_ptr<Buffer> indexBuffer_;
    uint32_t indexCount_ = 0;
};

} // namespace ne
