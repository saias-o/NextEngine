#include "graphics/ResourceManager.hpp"

#include "core/Log.hpp"
#include "graphics/Pipeline.hpp"
#include "graphics/Buffer.hpp"
#include "graphics/Material.hpp"
#include "graphics/Mesh.hpp"
#include "graphics/Primitives.hpp"
#include "graphics/Texture.hpp"
#include "project/AssetRegistry.hpp"
#include "scene/Node.hpp"
#include "scene/animation/Rig.hpp"
#include "scene/animation/AnimationClip.hpp"

#ifndef SAIDA_RHI_WEBGPU
#include "graphics/Swapchain.hpp"
#include "graphics/VulkanDevice.hpp"
#endif

#include <stb_image.h>

#include <array>
#include <atomic>
#include <stdexcept>
#include <string>

namespace saida {

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

ResourceManager::ResourceManager(rhi::Device& device, AssetRegistry* registry)
    : device_(device), registry_(registry) {
    geometryRegistry_ = std::make_unique<GeometryRegistry>(device_);
#ifdef SAIDA_RHI_WEBGPU
    materialSetLayout_ = std::make_unique<rhi::BindGroupLayout>(device_,
        std::vector<rhi::webgpu::BindGroupLayoutEntry>{
            {0, rhi::BindingType::SampledTexture, rhi::ShaderStages::Fragment}, // albedo texture
            {1, rhi::BindingType::SampledTexture, rhi::ShaderStages::Fragment}, // normal texture
            {2, rhi::BindingType::SampledTexture, rhi::ShaderStages::Fragment}, // metallic/roughness texture
            {3, rhi::BindingType::UniformBuffer, rhi::ShaderStages::Fragment},  // params
            {4, rhi::BindingType::SampledTexture, rhi::ShaderStages::Fragment}, // emissive texture
            {5, rhi::BindingType::Sampler, rhi::ShaderStages::Fragment},        // albedo sampler
            {6, rhi::BindingType::Sampler, rhi::ShaderStages::Fragment},        // normal sampler
            {7, rhi::BindingType::Sampler, rhi::ShaderStages::Fragment},        // metallic/roughness sampler
            {8, rhi::BindingType::Sampler, rhi::ShaderStages::Fragment},        // emissive sampler
        });
#else
    materialSetLayout_ = std::make_unique<rhi::BindGroupLayout>(device_,
        std::vector<rhi::BindGroupLayoutEntry>{
            {0, rhi::BindingType::CombinedImageSampler, rhi::ShaderStages::Fragment},  // albedo
            {1, rhi::BindingType::CombinedImageSampler, rhi::ShaderStages::Fragment},  // normal
            {2, rhi::BindingType::CombinedImageSampler, rhi::ShaderStages::Fragment},  // metallic/roughness
            {3, rhi::BindingType::UniformBuffer, rhi::ShaderStages::Fragment},         // params
            {4, rhi::BindingType::CombinedImageSampler, rhi::ShaderStages::Fragment},  // emissive
        });
    createGlobalBindlessResources();
#endif
    ensureDefaultTextures();
}

ResourceManager::~ResourceManager() {
    // Clear caches first (resource destructors run while the device is alive),
    // then the descriptor objects they were allocated from.
    materials_.clear();
    textures_.clear();
    meshes_.clear();
    rigs_.clear();
    animations_.clear();
    materialSetLayout_.reset();
#ifndef SAIDA_RHI_WEBGPU
    if (globalMaterialSetLayout_) vkDestroyDescriptorSetLayout(device_.device(), globalMaterialSetLayout_, nullptr);
    if (globalMaterialPool_) vkDestroyDescriptorPool(device_.device(), globalMaterialPool_, nullptr);
#endif
}

void ResourceManager::createGlobalBindlessResources() {
#ifdef SAIDA_RHI_WEBGPU
    return;
#else
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
        rhi::BufferUsage::Storage | rhi::BufferUsage::TransferDst,
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
#endif
}

uint32_t ResourceManager::getBindlessTextureIndex(Texture* texture) {
#ifdef SAIDA_RHI_WEBGPU
    (void)texture;
    return 0;
#else
    if (!globalMaterialSet_) return 0;
    if (!texture) return 0;
    if (nextBindlessTextureIndex_ >= kMaxBindlessTextures)
        throw std::runtime_error("bindless texture table exhausted");
    
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
#endif
}

uint32_t ResourceManager::ensureBindlessTextureIndex(Texture* texture) {
#ifdef SAIDA_RHI_WEBGPU
    (void)texture;
    return 0;
#else
    if (!texture) return 0;
    if (!globalMaterialSet_) return 0;
    if (texture->bindlessIndex() == ~0u) {
        texture->setBindlessIndex(getBindlessTextureIndex(texture));
    }
    return texture->bindlessIndex();
#endif
}


uint32_t ResourceManager::registerMaterialData(const glm::vec4& baseColor, const glm::vec4& emissive,
                                               float metallic, float roughness, float ao,
                                               uint32_t albedoIdx, uint32_t normalIdx, uint32_t mrIdx, uint32_t emissiveIdx) {
#ifdef SAIDA_RHI_WEBGPU
    (void)baseColor;
    (void)emissive;
    (void)metallic;
    (void)roughness;
    (void)ao;
    (void)albedoIdx;
    (void)normalIdx;
    (void)mrIdx;
    (void)emissiveIdx;
    return 0;
#else
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
#endif
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

Rig* ResourceManager::getRig(AssetID id) {
    if (id == kAssetInvalid) return nullptr;
    auto it = rigs_.find(id);
    if (it != rigs_.end()) return it->second.get();
    return nullptr;
}

AnimationClip* ResourceManager::getAnimation(AssetID id) {
    if (id == kAssetInvalid) return nullptr;
    auto it = animations_.find(id);
    if (it != animations_.end()) return it->second.get();
    return nullptr;
}

Texture* ResourceManager::getTexture(AssetID id, bool srgb) {
    if (auto it = textures_.find(id); it != textures_.end())
        return it->second.get();

    if (!registry_) return nullptr;
    // Resolve against the project root: registry paths are project-relative, so a
    // renamed/moved project dir still finds its assets (absolute paths would not).
    // Fall back to the raw path if there is no project root (already-absolute or
    // cwd-relative entries still load).
    std::string path = registry_->getAbsolutePath(id);
    if (path.empty()) path = registry_->getPath(id);
    if (path.empty()) return nullptr;

    auto tex = std::make_unique<Texture>(device_, path, srgb);
    ensureBindlessTextureIndex(tex.get());
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
    (void)srgb; // AssetRegistry tracks identity/type today, not texture import settings.
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
    
    rhi::Format format = srgb ? rhi::Format::RGBA8Srgb : rhi::Format::RGBA8Unorm;
    auto tex = std::make_unique<Texture>(device_, pixels, static_cast<uint32_t>(texWidth), static_cast<uint32_t>(texHeight), format);
    ensureBindlessTextureIndex(tex.get());
    stbi_image_free(pixels);
    
    static std::atomic<AssetID> s_dynamicId{0x8000000000000000ULL};
    AssetID id = s_dynamicId++;
    textures_.emplace(id, std::move(tex));
    return id;
}

AssetID ResourceManager::registerGeneratedTexture(const uint8_t* pixels, uint32_t width,
                                                  uint32_t height, rhi::Format format,
                                                  bool generateMipmaps) {
    if (!pixels || width == 0 || height == 0)
        return kAssetInvalid;

    auto tex = std::make_unique<Texture>(device_, pixels, width, height, format, generateMipmaps);
    ensureBindlessTextureIndex(tex.get());

    static std::atomic<AssetID> s_generatedId{0x8100000000000000ULL};
    AssetID id = s_generatedId++;
    textures_.emplace(id, std::move(tex));
    return id;
}

AssetID ResourceManager::registerMemoryMesh(const std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices) {
    static std::atomic<AssetID> s_dynamicId{0x4000000000000000ULL};
    AssetID id = s_dynamicId++;
    createMesh(id, vertices, indices);
    return id;
}

AssetID ResourceManager::registerMemoryRig(const std::string& path, std::unique_ptr<Rig> rig) {
    AssetID id = registry_ ? registry_->registerAsset(path, AssetType::Rig) : reinterpret_cast<AssetID>(rig.get());
    rigs_[id] = std::move(rig);
    return id;
}

AssetID ResourceManager::registerMemoryAnimation(const std::string& subPath, std::unique_ptr<AnimationClip> clip) {
    AssetID id = registry_ ? registry_->registerAsset(subPath, AssetType::Animation) : reinterpret_cast<AssetID>(clip.get());
    animations_[id] = std::move(clip);
    return id;
}

AssetID ResourceManager::meshId(const Mesh* mesh) const {
    auto it = reverseMeshMap_.find(mesh);
    return it != reverseMeshMap_.end() ? it->second : kAssetInvalid;
}

void ResourceManager::ensureDefaultTextures() {
    if (!defaultWhiteTexture_) {
        uint8_t whitePixels[] = {255, 255, 255, 255};
        defaultWhiteTexture_ = std::make_unique<Texture>(device_, whitePixels, 1, 1, rhi::Format::RGBA8Srgb);
        ensureBindlessTextureIndex(defaultWhiteTexture_.get());
    }
    if (!defaultNormalTexture_) {
        const uint8_t normalPixel[4] = {128, 128, 255, 255}; // 0.5, 0.5, 1.0 flat normal
        defaultNormalTexture_ = std::make_unique<Texture>(device_, normalPixel, 1, 1, rhi::Format::RGBA8Srgb);
        ensureBindlessTextureIndex(defaultNormalTexture_.get());
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

} // namespace saida
