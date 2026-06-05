#include "graphics/ResourceManager.hpp"

#include "core/Log.hpp"
#include "graphics/Pipeline.hpp"
#include "graphics/Swapchain.hpp"
#include "graphics/VulkanDevice.hpp"
#include "graphics/Buffer.hpp"
#include "graphics/Material.hpp"
#include "graphics/Mesh.hpp"
#include "graphics/Primitives.hpp"
#include "graphics/Texture.hpp"
#include "project/AssetRegistry.hpp"
#include "graphics/VulkanDevice.hpp"
#include "scene/Node.hpp"

#include <stb_image.h>

#include <array>
#include <atomic>
#include <stdexcept>
#include <string>

namespace ne {

namespace {
struct MaterialData {
    glm::vec4 baseColor;
    float metallic;
    float roughness;
    float ao;
    uint32_t albedoTexIdx;
    uint32_t normalTexIdx;
    uint32_t mrTexIdx;
    uint32_t emissiveTexIdx;
    uint32_t pad;
    glm::vec4 emissive;
};
}

ResourceManager::ResourceManager(VulkanDevice& device, AssetRegistry* registry)
    : device_(device), registry_(registry) {
    geometryRegistry_ = std::make_unique<GeometryRegistry>(device_);
    createMaterialSetLayout();
    materialPools_.push_back(createNewPool());
    createGlobalBindlessResources();
    ensureDefaultTextures();
}

ResourceManager::~ResourceManager() {
    // Clear caches first (resource destructors run while the device is alive),
    // then the descriptor objects they were allocated from.
    materials_.clear();
    textures_.clear();
    meshes_.clear();
    for (auto pool : materialPools_) {
        vkDestroyDescriptorPool(device_.device(), pool, nullptr);
    }
    vkDestroyDescriptorSetLayout(device_.device(), materialSetLayout_, nullptr);
    if (globalMaterialSetLayout_) vkDestroyDescriptorSetLayout(device_.device(), globalMaterialSetLayout_, nullptr);
    if (globalMaterialPool_) vkDestroyDescriptorPool(device_.device(), globalMaterialPool_, nullptr);
}

void ResourceManager::createMaterialSetLayout() {
    VkDescriptorSetLayoutBinding albedoBinding{};
    albedoBinding.binding = 0;
    albedoBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    albedoBinding.descriptorCount = 1;
    albedoBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutBinding normalBinding{};
    normalBinding.binding = 1;
    normalBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    normalBinding.descriptorCount = 1;
    normalBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutBinding mrBinding{};
    mrBinding.binding = 2;
    mrBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    mrBinding.descriptorCount = 1;
    mrBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutBinding paramsBinding{};
    paramsBinding.binding = 3;
    paramsBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    paramsBinding.descriptorCount = 1;
    paramsBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutBinding emissiveBinding{};
    emissiveBinding.binding = 4;
    emissiveBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    emissiveBinding.descriptorCount = 1;
    emissiveBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    std::array<VkDescriptorSetLayoutBinding, 5> bindings = {albedoBinding, normalBinding, mrBinding, paramsBinding, emissiveBinding};

    VkDescriptorSetLayoutCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    ci.bindingCount = static_cast<uint32_t>(bindings.size());
    ci.pBindings = bindings.data();
    if (vkCreateDescriptorSetLayout(device_.device(), &ci, nullptr, &materialSetLayout_) != VK_SUCCESS)
        throw std::runtime_error("failed to create material descriptor set layout");
}

VkDescriptorPool ResourceManager::createNewPool() {
    std::array<VkDescriptorPoolSize, 2> sizes{};
    sizes[0].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    sizes[0].descriptorCount = kMaxMaterials * 4; // 4 textures per material now
    sizes[1].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    sizes[1].descriptorCount = kMaxMaterials;

    VkDescriptorPoolCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    ci.poolSizeCount = static_cast<uint32_t>(sizes.size());
    ci.pPoolSizes = sizes.data();
    ci.maxSets = kMaxMaterials;
    
    VkDescriptorPool newPool = VK_NULL_HANDLE;
    if (vkCreateDescriptorPool(device_.device(), &ci, nullptr, &newPool) != VK_SUCCESS)
        throw std::runtime_error("failed to create material pool");
    return newPool;
}

void ResourceManager::createGlobalBindlessResources() {
    if (!device_.capabilities().descriptorIndexing) {
        Log::warn("Descriptor Indexing not supported. GPU-driven rendering is disabled.");
        return;
    }

    // Layout: Binding 0 = array of textures, Binding 1 = MaterialData SSBO
    VkDescriptorSetLayoutBinding texturesBinding{};
    texturesBinding.binding = 0;
    texturesBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    texturesBinding.descriptorCount = kMaxBindlessTextures;
    texturesBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutBinding materialBufferBinding{};
    materialBufferBinding.binding = 1;
    materialBufferBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    materialBufferBinding.descriptorCount = 1;
    materialBufferBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT;

    std::array<VkDescriptorSetLayoutBinding, 2> bindings = {texturesBinding, materialBufferBinding};

    VkDescriptorSetLayoutBindingFlagsCreateInfo flagsInfo{};
    flagsInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO;
    std::array<VkDescriptorBindingFlags, 2> bindingFlags = {
        VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT | VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT,
        0
    };
    flagsInfo.bindingCount = static_cast<uint32_t>(bindingFlags.size());
    flagsInfo.pBindingFlags = bindingFlags.data();

    VkDescriptorSetLayoutCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    ci.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT;
    ci.bindingCount = static_cast<uint32_t>(bindings.size());
    ci.pBindings = bindings.data();
    ci.pNext = &flagsInfo;

    if (vkCreateDescriptorSetLayout(device_.device(), &ci, nullptr, &globalMaterialSetLayout_) != VK_SUCCESS)
        throw std::runtime_error("failed to create global bindless descriptor set layout");

    std::array<VkDescriptorPoolSize, 2> poolSizes{};
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSizes[0].descriptorCount = kMaxBindlessTextures;
    poolSizes[1].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    poolSizes[1].descriptorCount = 1;

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT;
    poolInfo.maxSets = 1;
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();

    if (vkCreateDescriptorPool(device_.device(), &poolInfo, nullptr, &globalMaterialPool_) != VK_SUCCESS)
        throw std::runtime_error("failed to create global bindless pool");

    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = globalMaterialPool_;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &globalMaterialSetLayout_;

    if (vkAllocateDescriptorSets(device_.device(), &allocInfo, &globalMaterialSet_) != VK_SUCCESS)
        throw std::runtime_error("failed to allocate global bindless descriptor set");

    // Create global MaterialData SSBO
    VkDeviceSize ssboSize = kMaxBindlessMaterials * sizeof(MaterialData);
    globalMaterialBuffer_ = std::make_unique<Buffer>(device_, ssboSize,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        MemoryUsage::HostVisible); // MemoryUsage::HostVisible for simple CPU-side mapping

    // Write the SSBO to the descriptor set
    VkDescriptorBufferInfo bufferInfo{};
    bufferInfo.buffer = globalMaterialBuffer_->handle();
    bufferInfo.offset = 0;
    bufferInfo.range = VK_WHOLE_SIZE;

    VkWriteDescriptorSet write{};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = globalMaterialSet_;
    write.dstBinding = 1;
    write.dstArrayElement = 0;
    write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    write.descriptorCount = 1;
    write.pBufferInfo = &bufferInfo;

    vkUpdateDescriptorSets(device_.device(), 1, &write, 0, nullptr);
}

uint32_t ResourceManager::getBindlessTextureIndex(Texture* texture) {
    if (!globalMaterialSet_) return 0;
    
    uint32_t index = nextBindlessTextureIndex_++;
    
    VkDescriptorImageInfo imageInfo{};
    imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    imageInfo.imageView = texture->imageView();
    imageInfo.sampler = texture->sampler();

    VkWriteDescriptorSet write{};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = globalMaterialSet_;
    write.dstBinding = 0;
    write.dstArrayElement = index;
    write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.descriptorCount = 1;
    write.pImageInfo = &imageInfo;

    vkUpdateDescriptorSets(device_.device(), 1, &write, 0, nullptr);
    
    return index;
}


uint32_t ResourceManager::registerMaterialData(const glm::vec4& baseColor, const glm::vec4& emissive,
                                               float metallic, float roughness, float ao,
                                               uint32_t albedoIdx, uint32_t normalIdx, uint32_t mrIdx, uint32_t emissiveIdx) {
    if (!globalMaterialBuffer_) return 0;
    
    uint32_t index = nextMaterialIndex_++;
    if (index >= kMaxBindlessMaterials) {
        Log::warn("Global material buffer full! Cap is ", kMaxBindlessMaterials, ".");
        return 0; // fallback to 0
    }
    
    MaterialData data{};
    data.baseColor = baseColor;
    data.emissive = emissive;
    data.metallic = metallic;
    data.roughness = roughness;
    data.ao = ao;
    data.albedoTexIdx = albedoIdx;
    data.normalTexIdx = normalIdx;
    data.mrTexIdx = mrIdx;
    data.emissiveTexIdx = emissiveIdx;
    data.pad = 0;

    void* mapped = globalMaterialBuffer_->mapped();
    if (mapped) {
        memcpy(static_cast<char*>(mapped) + (index * sizeof(MaterialData)), &data, sizeof(MaterialData));
    }

    return index;
}

VkDescriptorSet ResourceManager::allocateMaterialSet(VkDescriptorSetLayout layout) {
    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = materialPools_.back();
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &layout;

    VkDescriptorSet set = VK_NULL_HANDLE;
    VkResult result = vkAllocateDescriptorSets(device_.device(), &allocInfo, &set);
    if (result == VK_ERROR_OUT_OF_POOL_MEMORY || result == VK_ERROR_FRAGMENTED_POOL) {
        materialPools_.push_back(createNewPool());
        allocInfo.descriptorPool = materialPools_.back();
        if (vkAllocateDescriptorSets(device_.device(), &allocInfo, &set) != VK_SUCCESS) {
            throw std::runtime_error("failed to allocate material descriptor set even after creating new pool");
        }
    } else if (result != VK_SUCCESS) {
        throw std::runtime_error("failed to allocate material descriptor set");
    }
    return set;
}

Mesh* ResourceManager::createMesh(AssetID id, const std::vector<Vertex>& vertices,
                                  const std::vector<uint32_t>& indices) {
    auto mesh = std::make_unique<Mesh>(*geometryRegistry_, vertices, indices);
    Mesh* ptr = mesh.get();
    meshes_.emplace(id, std::move(mesh));
    reverseMeshMap_[ptr] = id;
    return ptr;
}

Mesh* ResourceManager::loadMesh(AssetID id) {
    if (auto it = meshes_.find(id); it != meshes_.end())
        return it->second.get();
    
    if (!registry_) return nullptr;
    std::string path = registry_->getPath(id);
    if (path.empty()) return nullptr;

    auto mesh = Mesh::fromObjFile(*geometryRegistry_, path);
    Mesh* ptr = mesh.get();
    meshes_.emplace(id, std::move(mesh));
    reverseMeshMap_[ptr] = id;
    return ptr;
}

Mesh* ResourceManager::getMesh(AssetID id) {
    if (auto it = meshes_.find(id); it != meshes_.end())
        return it->second.get();
    if (id == kAssetBuiltinCube)
        return createMesh(id, cubeVertices(), cubeIndices());
    return loadMesh(id);  // treat as an .obj file path via registry
}

Texture* ResourceManager::getTexture(AssetID id, bool srgb) {
    if (auto it = textures_.find(id); it != textures_.end())
        return it->second.get();

    if (!registry_) return nullptr;
    std::string path = registry_->getPath(id);
    if (path.empty()) return nullptr;

    auto tex = std::make_unique<Texture>(device_, path, srgb);
    tex->setBindlessIndex(getBindlessTextureIndex(tex.get()));
    Texture* ptr = tex.get();
    textures_.emplace(id, std::move(tex));
    return ptr;
}

Material* ResourceManager::getMaterial(const MaterialDesc& desc) {
    if (auto it = materials_.find(desc); it != materials_.end())
        return it->second.get();

    auto material = std::make_unique<Material>(device_, *this, desc);
    Material* ptr = material.get();
    materials_.emplace(desc, std::move(material));
    return ptr;
}

AssetID ResourceManager::getOrRegister(const std::string& path, AssetType type, bool srgb) {
    if (!registry_) return kAssetInvalid;
    return registry_->registerAsset(path, type);
}

AssetID ResourceManager::registerMemoryTexture(const uint8_t* data, size_t size, bool srgb) {
    int texWidth, texHeight, texChannels;
    stbi_uc* pixels = stbi_load_from_memory(data, static_cast<int>(size), &texWidth, &texHeight, &texChannels, STBI_rgb_alpha);
    if (!pixels) {
        Log::error("Failed to load memory texture");
        return kAssetInvalid;
    }
    
    VkFormat format = srgb ? VK_FORMAT_R8G8B8A8_SRGB : VK_FORMAT_R8G8B8A8_UNORM;
    auto tex = std::make_unique<Texture>(device_, pixels, static_cast<uint32_t>(texWidth), static_cast<uint32_t>(texHeight), format);
    tex->setBindlessIndex(getBindlessTextureIndex(tex.get()));
    stbi_image_free(pixels);
    
    static std::atomic<AssetID> s_dynamicId{0x8000000000000000ULL};
    AssetID id = s_dynamicId++;
    textures_.emplace(id, std::move(tex));
    return id;
}

AssetID ResourceManager::registerMemoryMesh(const std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices) {
    static std::atomic<AssetID> s_dynamicId{0x4000000000000000ULL};
    AssetID id = s_dynamicId++;
    createMesh(id, vertices, indices);
    return id;
}

AssetID ResourceManager::meshId(const Mesh* mesh) const {
    auto it = reverseMeshMap_.find(mesh);
    return it != reverseMeshMap_.end() ? it->second : kAssetInvalid;
}

void ResourceManager::ensureDefaultTextures() {
    if (!defaultWhiteTexture_) {
        uint8_t whitePixels[] = {255, 255, 255, 255};
        defaultWhiteTexture_ = std::make_unique<Texture>(device_, whitePixels, 1, 1, VK_FORMAT_R8G8B8A8_SRGB);
        defaultWhiteTexture_->setBindlessIndex(getBindlessTextureIndex(defaultWhiteTexture_.get()));
    }
    if (!defaultNormalTexture_) {
        const uint8_t normalPixel[4] = {128, 128, 255, 255}; // 0.5, 0.5, 1.0 flat normal
        defaultNormalTexture_ = std::make_unique<Texture>(device_, normalPixel, 1, 1, VK_FORMAT_R8G8B8A8_SRGB);
        defaultNormalTexture_->setBindlessIndex(getBindlessTextureIndex(defaultNormalTexture_.get()));
    }
}

Texture* ResourceManager::defaultWhiteTexture() {
    ensureDefaultTextures();
    return defaultWhiteTexture_.get();
}

Texture* ResourceManager::defaultNormalTexture() {
    ensureDefaultTextures();
    return defaultNormalTexture_.get();
}

} // namespace ne
