#include "graphics/ResourceManager.hpp"

#include "core/Log.hpp"
#include "graphics/Material.hpp"
#include "graphics/Mesh.hpp"
#include "graphics/Primitives.hpp"
#include "graphics/Texture.hpp"
#include "graphics/VulkanDevice.hpp"

#include <array>
#include <stdexcept>
#include <string>

namespace ne {

namespace {
constexpr uint32_t kMaxMaterials = 64;
}

ResourceManager::ResourceManager(VulkanDevice& device) : device_(device) {
    createMaterialSetLayout();
    createMaterialPool();
}

ResourceManager::~ResourceManager() {
    // Clear caches first (resource destructors run while the device is alive),
    // then the descriptor objects they were allocated from.
    materials_.clear();
    textures_.clear();
    meshes_.clear();
    vkDestroyDescriptorPool(device_.device(), materialPool_, nullptr);
    vkDestroyDescriptorSetLayout(device_.device(), materialSetLayout_, nullptr);
}

void ResourceManager::createMaterialSetLayout() {
    VkDescriptorSetLayoutBinding samplerBinding{};
    samplerBinding.binding = 0;
    samplerBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    samplerBinding.descriptorCount = 1;
    samplerBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutBinding paramsBinding{};
    paramsBinding.binding = 1;
    paramsBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    paramsBinding.descriptorCount = 1;
    paramsBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    std::array<VkDescriptorSetLayoutBinding, 2> bindings = {samplerBinding, paramsBinding};

    VkDescriptorSetLayoutCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    ci.bindingCount = static_cast<uint32_t>(bindings.size());
    ci.pBindings = bindings.data();
    if (vkCreateDescriptorSetLayout(device_.device(), &ci, nullptr, &materialSetLayout_) != VK_SUCCESS)
        throw std::runtime_error("failed to create material descriptor set layout");
}

void ResourceManager::createMaterialPool() {
    std::array<VkDescriptorPoolSize, 2> sizes{};
    sizes[0].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    sizes[0].descriptorCount = kMaxMaterials;
    sizes[1].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    sizes[1].descriptorCount = kMaxMaterials;

    VkDescriptorPoolCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    ci.poolSizeCount = static_cast<uint32_t>(sizes.size());
    ci.pPoolSizes = sizes.data();
    ci.maxSets = kMaxMaterials;
    if (vkCreateDescriptorPool(device_.device(), &ci, nullptr, &materialPool_) != VK_SUCCESS)
        throw std::runtime_error("failed to create material descriptor pool");
}

Mesh* ResourceManager::createMesh(const std::string& id, const std::vector<Vertex>& vertices,
                                  const std::vector<uint32_t>& indices) {
    auto mesh = std::make_unique<Mesh>(device_, vertices, indices);
    Mesh* ptr = mesh.get();
    meshes_.emplace(id, std::move(mesh));
    reverseMeshMap_[ptr] = id;
    return ptr;
}

Mesh* ResourceManager::loadMesh(const std::string& path) {
    if (auto it = meshes_.find(path); it != meshes_.end())
        return it->second.get();
    auto mesh = Mesh::fromObjFile(device_, path);
    Mesh* ptr = mesh.get();
    meshes_.emplace(path, std::move(mesh));
    reverseMeshMap_[ptr] = path;
    return ptr;
}

Mesh* ResourceManager::getMesh(const std::string& id) {
    if (auto it = meshes_.find(id); it != meshes_.end())
        return it->second.get();
    if (id == "builtin:cube")
        return createMesh(id, cubeVertices(), cubeIndices());
    if (id.rfind("builtin:", 0) == 0) {
        Log::warn("getMesh: unknown built-in primitive '", id, "'");
        return nullptr;
    }
    return loadMesh(id);  // treat as an .obj file path
}

Texture* ResourceManager::getTexture(const std::string& path) {
    if (auto it = textures_.find(path); it != textures_.end())
        return it->second.get();
    auto texture = std::make_unique<Texture>(device_, path);
    Texture* ptr = texture.get();
    textures_.emplace(path, std::move(texture));
    return ptr;
}

Material* ResourceManager::getMaterial(const std::string& texturePath, const glm::vec4& baseColor) {
    // Content-addressed cache key: texture path + base color.
    std::string key = texturePath + "#" + std::to_string(baseColor.r) + "," +
        std::to_string(baseColor.g) + "," + std::to_string(baseColor.b) + "," +
        std::to_string(baseColor.a);
    if (auto it = materials_.find(key); it != materials_.end())
        return it->second.get();

    auto material = std::make_unique<Material>(device_, materialSetLayout_, materialPool_,
                                               getTexture(texturePath), texturePath, baseColor);
    Material* ptr = material.get();
    materials_.emplace(key, std::move(material));
    return ptr;
}

std::string ResourceManager::meshId(const Mesh* mesh) const {
    auto it = reverseMeshMap_.find(mesh);
    return it != reverseMeshMap_.end() ? it->second : "";
}

} // namespace ne
