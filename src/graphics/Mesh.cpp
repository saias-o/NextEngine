#include "graphics/Mesh.hpp"

#include "graphics/Buffer.hpp"
#include "graphics/VulkanDevice.hpp"
#include "tiny_obj_loader.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <unordered_map>

namespace ne {

VkVertexInputBindingDescription Vertex::bindingDescription() {
    VkVertexInputBindingDescription desc{};
    desc.binding = 0;
    desc.stride = sizeof(Vertex);
    desc.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    return desc;
}

std::array<VkVertexInputAttributeDescription, 3> Vertex::attributeDescriptions() {
    std::array<VkVertexInputAttributeDescription, 3> attrs{};
    attrs[0].binding = 0;
    attrs[0].location = 0;
    attrs[0].format = VK_FORMAT_R32G32B32_SFLOAT;
    attrs[0].offset = offsetof(Vertex, pos);
    attrs[1].binding = 0;
    attrs[1].location = 1;
    attrs[1].format = VK_FORMAT_R32G32B32_SFLOAT;
    attrs[1].offset = offsetof(Vertex, color);
    attrs[2].binding = 0;
    attrs[2].location = 2;
    attrs[2].format = VK_FORMAT_R32G32_SFLOAT;
    attrs[2].offset = offsetof(Vertex, texCoord);
    return attrs;
}

Mesh::Mesh(VulkanDevice& device, const std::vector<Vertex>& vertices,
           const std::vector<uint32_t>& indices)
    : device_(device), indexCount_(static_cast<uint32_t>(indices.size())) {
    createVertexBuffer(vertices);
    createIndexBuffer(indices);
}

Mesh::~Mesh() = default;

std::unique_ptr<Mesh> Mesh::fromObjFile(VulkanDevice& device, const std::string& path) {
    tinyobj::attrib_t attrib;
    std::vector<tinyobj::shape_t> shapes;
    std::vector<tinyobj::material_t> materials;
    std::string warn, err;

    std::string baseDir;
    if (auto slash = path.find_last_of("/\\"); slash != std::string::npos)
        baseDir = path.substr(0, slash + 1);

    // triangulate=true is the default: polygon faces (e.g. quads) are split.
    if (!tinyobj::LoadObj(&attrib, &shapes, &materials, &warn, &err, path.c_str(), baseDir.c_str()))
        throw std::runtime_error("failed to load model '" + path + "': " + warn + err);

    // Compute the bounding box to recenter the model on the origin and scale it
    // uniformly so its largest dimension fits within a unit cube.
    glm::vec3 mn(std::numeric_limits<float>::max());
    glm::vec3 mx(std::numeric_limits<float>::lowest());
    for (size_t i = 0; i + 2 < attrib.vertices.size(); i += 3) {
        glm::vec3 p(attrib.vertices[i], attrib.vertices[i + 1], attrib.vertices[i + 2]);
        mn = glm::min(mn, p);
        mx = glm::max(mx, p);
    }
    glm::vec3 center = (mn + mx) * 0.5f;
    glm::vec3 dim = mx - mn;
    float extent = std::max({dim.x, dim.y, dim.z});
    float scale = extent > 0.0f ? 1.0f / extent : 1.0f;

    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    std::unordered_map<uint64_t, uint32_t> uniqueVertices;

    for (const auto& shape : shapes) {
        for (const auto& idx : shape.mesh.indices) {
            // De-duplicate on the (position, normal) index pair.
            uint64_t key = (static_cast<uint64_t>(static_cast<uint32_t>(idx.vertex_index)) << 32)
                         | static_cast<uint32_t>(idx.normal_index);
            if (auto it = uniqueVertices.find(key); it != uniqueVertices.end()) {
                indices.push_back(it->second);
                continue;
            }

            Vertex v{};
            v.pos = (glm::vec3(attrib.vertices[3 * idx.vertex_index + 0],
                               attrib.vertices[3 * idx.vertex_index + 1],
                               attrib.vertices[3 * idx.vertex_index + 2]) - center) * scale;
            if (idx.normal_index >= 0) {
                glm::vec3 n(attrib.normals[3 * idx.normal_index + 0],
                            attrib.normals[3 * idx.normal_index + 1],
                            attrib.normals[3 * idx.normal_index + 2]);
                v.color = n * 0.5f + 0.5f;  // map [-1,1] normal to a [0,1] color
            } else {
                v.color = glm::vec3(0.8f);
            }
            if (idx.texcoord_index >= 0) {
                v.texCoord = glm::vec2(attrib.texcoords[2 * idx.texcoord_index + 0],
                                       1.0f - attrib.texcoords[2 * idx.texcoord_index + 1]);
            } else {
                v.texCoord = glm::vec2(0.0f);
            }

            uint32_t newIndex = static_cast<uint32_t>(vertices.size());
            uniqueVertices[key] = newIndex;
            vertices.push_back(v);
            indices.push_back(newIndex);
        }
    }

    std::cout << "loaded '" << path << "': " << vertices.size() << " vertices, "
              << indices.size() / 3 << " triangles" << std::endl;

    return std::make_unique<Mesh>(device, vertices, indices);
}

void Mesh::createVertexBuffer(const std::vector<Vertex>& vertices) {
    VkDeviceSize bufferSize = sizeof(vertices[0]) * vertices.size();

    Buffer staging(device_, bufferSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        MemoryUsage::HostVisible);
    staging.write(vertices.data(), bufferSize);

    vertexBuffer_ = std::make_unique<Buffer>(device_, bufferSize,
        VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
        MemoryUsage::GpuOnly);

    device_.copyBuffer(staging.handle(), vertexBuffer_->handle(), bufferSize);
}

void Mesh::createIndexBuffer(const std::vector<uint32_t>& indices) {
    VkDeviceSize bufferSize = sizeof(indices[0]) * indices.size();

    Buffer staging(device_, bufferSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        MemoryUsage::HostVisible);
    staging.write(indices.data(), bufferSize);

    indexBuffer_ = std::make_unique<Buffer>(device_, bufferSize,
        VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
        MemoryUsage::GpuOnly);

    device_.copyBuffer(staging.handle(), indexBuffer_->handle(), bufferSize);
}

void Mesh::bind(VkCommandBuffer cmd) const {
    VkBuffer buffers[] = {vertexBuffer_->handle()};
    VkDeviceSize offsets[] = {0};
    vkCmdBindVertexBuffers(cmd, 0, 1, buffers, offsets);
    vkCmdBindIndexBuffer(cmd, indexBuffer_->handle(), 0, VK_INDEX_TYPE_UINT32);
}

void Mesh::draw(VkCommandBuffer cmd) const {
    vkCmdDrawIndexed(cmd, indexCount_, 1, 0, 0, 0);
}

} // namespace ne
