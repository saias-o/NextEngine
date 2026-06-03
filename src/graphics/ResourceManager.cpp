#include "graphics/ResourceManager.hpp"

#include "graphics/Material.hpp"
#include "graphics/Mesh.hpp"
#include "graphics/Texture.hpp"
#include "graphics/VulkanDevice.hpp"

#include <array>
#include <stdexcept>

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

Mesh* ResourceManager::loadMesh(const std::string& path) {
    if (auto it = meshes_.find(path); it != meshes_.end())
        return it->second.get();
    auto mesh = Mesh::fromObjFile(device_, path);
    Mesh* ptr = mesh.get();
    meshes_.emplace(path, std::move(mesh));
    return ptr;
}

Mesh* ResourceManager::createMesh(const std::string& key, const std::vector<Vertex>& vertices,
                                  const std::vector<uint32_t>& indices) {
    if (auto it = meshes_.find(key); it != meshes_.end())
        return it->second.get();
    auto mesh = std::make_unique<Mesh>(device_, vertices, indices);
    Mesh* ptr = mesh.get();
    meshes_.emplace(key, std::move(mesh));
    return ptr;
}

Texture* ResourceManager::loadTexture(const std::string& path) {
    if (auto it = textures_.find(path); it != textures_.end())
        return it->second.get();
    auto texture = std::make_unique<Texture>(device_, path);
    Texture* ptr = texture.get();
    textures_.emplace(path, std::move(texture));
    return ptr;
}

Material* ResourceManager::createMaterial(const std::string& key, Texture* texture,
                                          const glm::vec4& baseColor) {
    if (auto it = materials_.find(key); it != materials_.end())
        return it->second.get();
    auto material = std::make_unique<Material>(device_, materialSetLayout_, materialPool_,
                                               texture, baseColor);
    Material* ptr = material.get();
    materials_.emplace(key, std::move(material));
    return ptr;
}

Mesh* ResourceManager::mesh(const std::string& key) const {
    auto it = meshes_.find(key);
    return it != meshes_.end() ? it->second.get() : nullptr;
}

Material* ResourceManager::material(const std::string& key) const {
    auto it = materials_.find(key);
    return it != materials_.end() ? it->second.get() : nullptr;
}

std::string ResourceManager::meshKey(const Mesh* mesh) const {
    for (auto& [key, ptr] : meshes_)
        if (ptr.get() == mesh) return key;
    return {};
}

std::string ResourceManager::materialKey(const Material* material) const {
    for (auto& [key, ptr] : materials_)
        if (ptr.get() == material) return key;
    return {};
}

} // namespace ne
