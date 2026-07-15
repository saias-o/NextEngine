#include "graphics/ResourceManager.hpp"

#include "core/Log.hpp"
#include "core/Profiler.hpp"
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
#include "scene/animation/AnimGraphAsset.hpp"
#include "scene/animation/ClipView.hpp"

#ifndef SAIDA_RHI_WEBGPU
#include "graphics/Swapchain.hpp"
#include "graphics/VulkanDevice.hpp"
#endif

#include <stb_image.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstring>
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
    uint32_t materialType;
    glm::vec4 emissive;
};
static_assert(sizeof(MaterialData) == 64, "MaterialData must match shader.frag std430 layout");

// Image décodée sur le worker de l'AssetLoader : pixels RGBA prêts pour
// l'upload GPU (fait sur le thread principal dans finalizePendingTextures).
struct DecodedImage {
    void* pixels = nullptr;  // allocation stbi
    uint32_t width = 0;
    uint32_t height = 0;
    bool hdr = false;  // pixels en float RGBA (16 o/px), sinon RGBA8
    ~DecodedImage() {
        if (pixels) stbi_image_free(pixels);
    }
};

// Décodage stbi pur CPU — exécuté hors du thread principal sur desktop. Le
// HDR (.hdr → RGBA32F) n'est décodé en float que sur desktop ; le backend web
// suit son comportement historique (stbi convertit en LDR).
AssetDecoder makeImageDecoder() {
    return [](std::vector<uint8_t>&& bytes, AssetDecodeResult& out, std::string& error) {
        auto image = std::make_shared<DecodedImage>();
        int w = 0, h = 0, channels = 0;
        const auto* data = bytes.data();
        const int size = static_cast<int>(bytes.size());
#ifndef SAIDA_RHI_WEBGPU
        image->hdr = size > 0 && stbi_is_hdr_from_memory(data, size) != 0;
        if (image->hdr)
            image->pixels = stbi_loadf_from_memory(data, size, &w, &h, &channels, STBI_rgb_alpha);
        else
#endif
            image->pixels = stbi_load_from_memory(data, size, &w, &h, &channels, STBI_rgb_alpha);
        if (!image->pixels) {
            const char* reason = stbi_failure_reason();
            error = reason ? reason : "unrecognized image";
            return false;
        }
        image->width = static_cast<uint32_t>(w);
        image->height = static_cast<uint32_t>(h);
        out.payload = image;
        out.bytes = static_cast<uint64_t>(image->width) * image->height * (image->hdr ? 16 : 4);
        return true;
    };
}

// Parse .obj pur CPU — exécuté hors du thread principal sur desktop. L'upload
// GPU (GeometryRegistry) reste sur le thread principal (finalizePendingMeshes).
AssetDecoder makeMeshDecoder() {
    return [](std::vector<uint8_t>&& bytes, AssetDecodeResult& out, std::string& error) {
        auto data = std::make_shared<MeshData>();
        if (!Mesh::parseObjBytes(bytes.data(), bytes.size(), *data, error)) return false;
        out.payload = data;
        out.bytes = static_cast<uint64_t>(data->vertices.size()) * sizeof(Vertex) +
                    static_cast<uint64_t>(data->indices.size()) * sizeof(uint32_t);
        return true;
    };
}
}

ResourceManager::ResourceManager(rhi::Device& device, AssetRegistry* registry)
    : device_(device), registry_(registry) {
    assetLoader_ = std::make_unique<AssetLoader>(registry_);
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
    assetLoader_.reset();
    pendingTextures_.clear();
    pendingMeshes_.clear();
    // Clear caches first (resource destructors run while the device is alive),
    // then the descriptor objects they were allocated from. The graveyard goes
    // with them (retired bind groups were allocated from the same layout/pool).
    graveyard_.clear();
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

void ResourceManager::setRegistry(AssetRegistry* registry) {
    registry_ = registry;
    assetLoader_->setRegistry(registry);
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

    // Réutilise un index recyclé par le graveyard avant d'étendre la table.
    uint32_t index;
    if (!freeBindlessIndices_.empty()) {
        index = freeBindlessIndices_.back();
        freeBindlessIndices_.pop_back();
    } else {
        if (nextBindlessTextureIndex_ >= kMaxBindlessTextures)
            throw std::runtime_error("bindless texture table exhausted");
        index = nextBindlessTextureIndex_++;
    }

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


void ResourceManager::writeMaterialSlot(uint32_t index, const glm::vec4& baseColor,
                                        const glm::vec4& emissive,
                                        float metallic, float roughness, float ao,
                                        uint32_t albedoIdx, uint32_t normalIdx, uint32_t mrIdx,
                                        uint32_t emissiveIdx, MaterialType type) {
#ifdef SAIDA_RHI_WEBGPU
    (void)index;
    (void)baseColor;
    (void)emissive;
    (void)metallic;
    (void)roughness;
    (void)ao;
    (void)albedoIdx;
    (void)normalIdx;
    (void)mrIdx;
    (void)emissiveIdx;
    (void)type;
#else
    if (!globalMaterialBuffer_) return;

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
    data.materialType = static_cast<uint32_t>(type);

    void* mapped = globalMaterialBuffer_->mapped();
    if (mapped) {
        const uint64_t offset = uint64_t(index) * sizeof(MaterialData);
        memcpy(static_cast<char*>(mapped) + offset, &data, sizeof(MaterialData));
        // VMA allocations may be non-coherent; publishing material data without
        // this flush made newly-created bindless materials platform-dependent.
        globalMaterialBuffer_->flush(sizeof(MaterialData), offset);
    }
#endif
}

uint32_t ResourceManager::registerMaterialData(const glm::vec4& baseColor, const glm::vec4& emissive,
                                               float metallic, float roughness, float ao,
                                               uint32_t albedoIdx, uint32_t normalIdx, uint32_t mrIdx,
                                               uint32_t emissiveIdx, MaterialType type) {
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
    (void)type;
    return 0;
#else
    if (!globalMaterialBuffer_) return 0;

    // Réutilise un slot libéré par l'éviction d'un matériau avant d'étendre.
    uint32_t index;
    if (!freeMaterialIndices_.empty()) {
        index = freeMaterialIndices_.back();
        freeMaterialIndices_.pop_back();
    } else {
        index = nextMaterialIndex_++;
        if (index >= kMaxBindlessMaterials) {
            Log::warn("Global material buffer full! Cap is ", kMaxBindlessMaterials, ".");
            return 0; // fallback to 0
        }
    }

    writeMaterialSlot(index, baseColor, emissive, metallic, roughness, ao,
                      albedoIdx, normalIdx, mrIdx, emissiveIdx, type);
    return index;
#endif
}

void ResourceManager::updateMaterialData(uint32_t index, const glm::vec4& baseColor,
                                         const glm::vec4& emissive,
                                         float metallic, float roughness, float ao,
                                         uint32_t albedoIdx, uint32_t normalIdx, uint32_t mrIdx,
                                         uint32_t emissiveIdx, MaterialType type) {
    writeMaterialSlot(index, baseColor, emissive, metallic, roughness, ao,
                      albedoIdx, normalIdx, mrIdx, emissiveIdx, type);
}

Mesh* ResourceManager::createMesh(AssetID id, const std::vector<Vertex>& vertices,
                                  const std::vector<uint32_t>& indices) {
    auto mesh = std::make_unique<Mesh>(*geometryRegistry_, vertices, indices);
    Mesh* ptr = mesh.get();
    gpuResidentBytes_ += ptr->gpuBytes();
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

    // Chargement asynchrone (chantier 3) : le parse .obj part sur le worker et
    // l'appelant reçoit tout de suite un proxy stable — draw() est un no-op et
    // les colliders Auto attendent la géométrie (meshPending). Le proxy est
    // rempli par finalizePendingMeshes().
    AssetHandle handle = assetLoader_->request(id, AssetLoadPriority::High,
                                               AssetPayloadKind::MeshObj, makeMeshDecoder());
    if (!handle) return nullptr;

    auto mesh = std::make_unique<Mesh>(*geometryRegistry_);
    Mesh* ptr = mesh.get();
    meshes_.emplace(id, std::move(mesh));
    reverseMeshMap_[ptr] = id;
    pendingMeshes_.emplace(id, std::move(handle));
    return ptr;
}

void ResourceManager::finalizePendingMeshes() {
    if (pendingMeshes_.empty()) return;
    for (auto it = pendingMeshes_.begin(); it != pendingMeshes_.end();) {
        const AssetLoadState state = it->second.state();
        if (state == AssetLoadState::Queued || state == AssetLoadState::Loading) {
            ++it;
            continue;
        }
        const AssetID id = it->first;
        if (state == AssetLoadState::Ready) {
            if (auto data = std::static_pointer_cast<MeshData>(it->second.payload())) {
                SAIDA_PROFILE_SCOPE("Resource/FinalizeAsyncMesh");
                if (auto meshIt = meshes_.find(id); meshIt != meshes_.end()) {
                    meshIt->second->upload(data->vertices, data->indices);
                    gpuResidentBytes_ += meshIt->second->gpuBytes();
                    Log::info("loaded '", registry_ ? registry_->getPath(id) : std::string(), "': ",
                              data->vertices.size(), " vertices, ",
                              data->indices.size() / 3, " triangles");
                }
            }
        }
        // Échec : le proxy reste vide (rien à dessiner), le diagnostic est
        // déjà loggué par le loader. Le handle relâché libère la géométrie
        // CPU décodée de la comptabilité du loader.
        it = pendingMeshes_.erase(it);
    }
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
    if (id == kAssetInvalid) return nullptr;
    if (auto it = textures_.find(id); it != textures_.end())
        return it->second.get();
    // Un asset en échec rend le damier magenta (fallback visible) au lieu de
    // relancer un chargement voué à l'échec à chaque frame.
    if (failedTextures_.count(id)) return missingTexture();
    if (!registry_) return nullptr;

    // Chargement asynchrone : lecture fichier + décodage stbi sur le worker
    // (l'AssetLoader résout le chemin via le registre — relatif à la racine
    // projet, donc robuste au déplacement du dossier). L'appelant retombe sur
    // ses fallbacks ; finalizePendingTextures() crée la texture GPU et rebinde
    // les matériaux quand les pixels sont prêts.
    if (!pendingTextures_.count(id)) {
        AssetHandle handle = assetLoader_->request(id, AssetLoadPriority::High,
                                                   AssetPayloadKind::Image, makeImageDecoder());
        if (!handle) return nullptr;
        pendingTextures_.emplace(id, PendingTexture{srgb, std::move(handle)});
    }
    return nullptr;
}

void ResourceManager::finalizePendingTextures() {
    if (pendingTextures_.empty()) return;
    for (auto it = pendingTextures_.begin(); it != pendingTextures_.end();) {
        const AssetLoadState state = it->second.handle.state();
        if (state == AssetLoadState::Queued || state == AssetLoadState::Loading) {
            ++it;
            continue;
        }
        const AssetID id = it->first;
        bool created = false;
        if (state == AssetLoadState::Ready) {
            if (auto image = std::static_pointer_cast<DecodedImage>(it->second.handle.payload())) {
                SAIDA_PROFILE_SCOPE("Resource/FinalizeAsyncTexture");
                rhi::Format format = it->second.srgb ? rhi::Format::RGBA8Srgb : rhi::Format::RGBA8Unorm;
#ifndef SAIDA_RHI_WEBGPU
                if (image->hdr) format = rhi::Format::RGBA32Float;
#endif
                auto tex = std::make_unique<Texture>(
                    device_, static_cast<const uint8_t*>(image->pixels),
                    image->width, image->height, format);
                ensureBindlessTextureIndex(tex.get());
                gpuResidentBytes_ += tex->gpuBytes();
                textures_.emplace(id, std::move(tex));
                created = true;
            }
        }
        if (!created) failedTextures_.insert(id);  // diagnostic déjà loggué par le loader
        // Libère le handle (les pixels décodés sortent de la comptabilité du
        // loader), puis rebinde les matériaux qui référencent cet asset.
        it = pendingTextures_.erase(it);
        rebindMaterialsUsing(id);
    }
}

void ResourceManager::rebindMaterialsUsing(AssetID textureId) {
    for (auto& [desc, material] : materials_) {
        if (desc.albedoId == textureId || desc.normalId == textureId ||
            desc.metallicRoughnessId == textureId || desc.emissiveId == textureId)
            material->rebindTextures(*this);
    }
}

void ResourceManager::trimUnused(const AssetUsage& used) {
    SAIDA_PROFILE_SCOPE("Resource/TrimUnused");
    const size_t texturesBefore = textures_.size();
    const size_t meshesBefore = meshes_.size();
    const size_t materialsBefore = materials_.size();

    // Textures : tout ce qu'aucun matériau/nœud/skybox vivant ne référence.
    for (auto it = textures_.begin(); it != textures_.end();) {
        if (used.textures.count(it->first)) {
            ++it;
            continue;
        }
        Retired r;
        r.bindlessIndex = it->second->bindlessIndex();
        gpuResidentBytes_ -= std::min(gpuResidentBytes_, it->second->gpuBytes());
        r.texture = std::move(it->second);
        r.frame = frameClock_;
        graveyard_.push_back(std::move(r));
        it = textures_.erase(it);
    }

    // Meshes : builtins et proxies en attente de géométrie exempts.
    for (auto it = meshes_.begin(); it != meshes_.end();) {
        if (it->first == kAssetBuiltinCube || used.meshes.count(it->second.get()) ||
            pendingMeshes_.count(it->first)) {
            ++it;
            continue;
        }
        gpuResidentBytes_ -= std::min(gpuResidentBytes_, it->second->gpuBytes());
        reverseMeshMap_.erase(it->second.get());
        Retired r;
        r.mesh = std::move(it->second);
        r.frame = frameClock_;
        graveyard_.push_back(std::move(r));
        it = meshes_.erase(it);
    }

    // Matériaux : un matériau utilisé garde ses textures vivantes (marquées
    // par le collecteur), donc l'inverse — évincer un matériau dont les
    // textures survivent ailleurs — est sain.
    for (auto it = materials_.begin(); it != materials_.end();) {
        if (used.materials.count(it->second.get())) {
            ++it;
            continue;
        }
        Retired r;
        r.materialIndex = it->second->bindlessIndex();
        // Le slot 0 sert aussi de fallback quand la table est pleine : il peut
        // être partagé par plusieurs matériaux, on ne le recycle jamais.
        if (r.materialIndex == 0) r.materialIndex = ~0u;
        r.material = std::move(it->second);
        r.frame = frameClock_;
        graveyard_.push_back(std::move(r));
        it = materials_.erase(it);
    }

    const size_t evicted = (texturesBefore - textures_.size()) +
                           (meshesBefore - meshes_.size()) +
                           (materialsBefore - materials_.size());
    if (evicted)
        Log::info("assets: evicted ", texturesBefore - textures_.size(), " textures, ",
                  meshesBefore - meshes_.size(), " meshes, ",
                  materialsBefore - materials_.size(), " materials (gpu resident ",
                  gpuResidentBytes_, " bytes)");
}

void ResourceManager::retireBindGroup(std::unique_ptr<rhi::BindGroup> group) {
    if (!group) return;
    Retired r;
    r.bindGroup = std::move(group);
    r.frame = frameClock_;
    graveyard_.push_back(std::move(r));
}

void ResourceManager::drainGraveyard() {
    for (auto it = graveyard_.begin(); it != graveyard_.end();) {
        if (frameClock_ - it->frame < kRetireFrames) {
            ++it;
            continue;
        }
        // Plus aucune frame en vol ne référence l'objet : l'index bindless
        // repointe sur la texture blanche puis redevient allouable, le slot
        // matériau retourne en freelist, et les destructeurs libèrent le GPU.
        if (it->bindlessIndex != ~0u) {
#ifndef SAIDA_RHI_WEBGPU
            if (globalMaterialSet_) {
                ensureDefaultTextures();
                VkDescriptorImageInfo imageInfo{};
                imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                imageInfo.imageView = defaultWhiteTexture_->imageView();
                imageInfo.sampler = defaultWhiteTexture_->sampler();
                VkWriteDescriptorSet write{};
                write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                write.dstSet = globalMaterialSet_;
                write.dstBinding = 0;
                write.dstArrayElement = it->bindlessIndex;
                write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                write.descriptorCount = 1;
                write.pImageInfo = &imageInfo;
                vkUpdateDescriptorSets(device_.device(), 1, &write, 0, nullptr);
            }
#endif
            freeBindlessIndices_.push_back(it->bindlessIndex);
        }
        if (it->materialIndex != ~0u) freeMaterialIndices_.push_back(it->materialIndex);
        it = graveyard_.erase(it);
    }
}

void ResourceManager::pumpAssetLoads() {
    ++frameClock_;
    drainGraveyard();
    // pump() d'abord : sur le web (pas de worker) c'est lui qui exécute les
    // chargements — finaliser ensuite rend les assets prêts dès cette frame.
    assetLoader_->pump();
    finalizePendingTextures();
    finalizePendingMeshes();
    SAIDA_PROFILE_COUNTER("Assets/GpuResidentBytes", gpuResidentBytes_);
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
    gpuResidentBytes_ += tex->gpuBytes();

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
    gpuResidentBytes_ += tex->gpuBytes();

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

AssetID ResourceManager::loadClipView(const std::string& path) {
    auto parsed = ClipView::loadFile(path);
    if (!parsed.ok) {
        for (const auto& d : parsed.diagnostics)
            Log::error("ClipView '", path, "': [", d.code, "] ", d.message);
        return kAssetInvalid;
    }
    auto view = std::make_unique<ClipView>(std::move(parsed.view));
    AssetID id = registry_ ? registry_->registerAsset(path, AssetType::Animation)
                           : reinterpret_cast<AssetID>(view.get());
    clipViews_[id] = std::move(view);
    return id;
}

const ClipView* ResourceManager::getClipView(AssetID id) const {
    auto it = clipViews_.find(id);
    return it != clipViews_.end() ? it->second.get() : nullptr;
}

AssetID ResourceManager::loadAnimGraph(const std::string& path) {
    auto parsed = AnimGraphAsset::loadFile(path);
    if (!parsed.ok) {
        for (const auto& d : parsed.diagnostics)
            Log::error("AnimGraph '", path, "': [", d.code, "] ", d.message);
        return kAssetInvalid;
    }
    for (const auto& d : parsed.graph.validate()) {
        if (d.severity == AssetDiagnostic::Severity::Error) {
            Log::error("AnimGraph '", path, "': [", d.code, "] ", d.message);
            return kAssetInvalid;
        }
        Log::warn("AnimGraph '", path, "': [", d.code, "] ", d.message);
    }
    auto graph = std::make_unique<AnimGraphAsset>(std::move(parsed.graph));
    AssetID id = registry_ ? registry_->registerAsset(path, AssetType::Animation)
                           : reinterpret_cast<AssetID>(graph.get());
    animGraphs_[id] = std::move(graph);
    return id;
}

const AnimGraphAsset* ResourceManager::getAnimGraph(AssetID id) const {
    auto it = animGraphs_.find(id);
    return it != animGraphs_.end() ? it->second.get() : nullptr;
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

AssetID ResourceManager::animationId(const AnimationClip* clip) const {
    for (const auto& [id, owned] : animations_)
        if (owned.get() == clip) return id;
    return kAssetInvalid;
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

Texture* ResourceManager::missingTexture() {
    if (!missingTexture_) {
        // Damier magenta/anthracite 2x2 — le fallback visible du chantier 3 :
        // un asset manquant se voit, il ne fait ni crash ni surface noire.
        const uint8_t px[2 * 2 * 4] = {
            255, 0, 255, 255,  24, 24, 24, 255,
            24, 24, 24, 255,  255, 0, 255, 255,
        };
        missingTexture_ = std::make_unique<Texture>(device_, px, 2, 2, rhi::Format::RGBA8Srgb);
        ensureBindlessTextureIndex(missingTexture_.get());
    }
    return missingTexture_.get();
}

} // namespace saida
