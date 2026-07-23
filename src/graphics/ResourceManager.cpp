#include "graphics/ResourceManager.hpp"

#include "core/Log.hpp"
#include "core/Profiler.hpp"
#include "graphics/Pipeline.hpp"
#include "graphics/Buffer.hpp"
#include "graphics/Material.hpp"
#include "graphics/Texture.hpp"
#include "project/AssetRegistry.hpp"
#include "scene/Node.hpp"
#include "scene/animation/Rig.hpp"
#include "scene/animation/RigAsset.hpp"
#include "scene/animation/AnimationClip.hpp"
#include "scene/animation/AnimationAssetDecoders.hpp"
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
// utilise volontairement la conversion LDR de stbi.
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

}

namespace {
// Shared by the three animation-asset extractors below.
void logAnimationAssetDiagnostics(const char* type, const std::string& path,
                                  const std::vector<AssetDiagnostic>& diagnostics) {
    for (const AssetDiagnostic& diagnostic : diagnostics) {
        if (diagnostic.severity == AssetDiagnostic::Severity::Error)
            Log::error(type, " '", path, "': [", diagnostic.code, "] ",
                       diagnostic.message);
        else
            Log::warn(type, " '", path, "': [", diagnostic.code, "] ",
                      diagnostic.message);
    }
}
} // namespace

ResourceManager::ResourceManager(rhi::Device& device, AssetRegistry* registry)
    : device_(device), registry_(registry),
      bindlessTables_(device, kMaxBindlessTextures, kMaxBindlessMaterials),
      rigAssetCache_(AssetType::Rig, AssetPayloadKind::RigAsset, makeRigAssetDecoder,
                     [](const std::shared_ptr<void>& p, const std::string& path)
                         -> std::unique_ptr<RigAsset> {
                         auto payload = std::static_pointer_cast<DecodedRigAsset>(p);
                         if (!payload) return nullptr;
                         logAnimationAssetDiagnostics("RigAsset", path, payload->diagnostics);
                         return std::make_unique<RigAsset>(std::move(payload->asset));
                     }),
      clipViewCache_(AssetType::Animation, AssetPayloadKind::ClipView, makeClipViewDecoder,
                     [](const std::shared_ptr<void>& p, const std::string& path)
                         -> std::unique_ptr<ClipView> {
                         auto payload = std::static_pointer_cast<DecodedClipView>(p);
                         if (!payload) return nullptr;
                         logAnimationAssetDiagnostics("ClipView", path, payload->diagnostics);
                         return std::make_unique<ClipView>(std::move(payload->view));
                     }),
      animGraphCache_(AssetType::Animation, AssetPayloadKind::AnimGraph, makeAnimGraphDecoder,
                      [](const std::shared_ptr<void>& p, const std::string& path)
                          -> std::unique_ptr<AnimGraphAsset> {
                          auto payload = std::static_pointer_cast<DecodedAnimGraph>(p);
                          if (!payload) return nullptr;
                          logAnimationAssetDiagnostics("AnimGraph", path, payload->diagnostics);
                          return std::make_unique<AnimGraphAsset>(std::move(payload->graph));
                      }) {
    assetLoader_ = std::make_unique<AssetLoader>(registry_);
    geometryRegistry_ = std::make_unique<GeometryRegistry>(device_);
    meshCache_ = std::make_unique<MeshCache>(*geometryRegistry_);
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
    bindlessTables_.create();
#endif
    ensureDefaultTextures();
}

ResourceManager::~ResourceManager() {
    assetLoader_.reset();
    pendingTextures_.clear();
    // Clear caches first (resource destructors run while the device is alive),
    // then the descriptor objects they were allocated from. The graveyard goes
    // with them (retired bind groups were allocated from the same layout/pool).
    graveyard_.clear();
    materials_.clear();
    textures_.clear();
    meshCache_->clear();
    rigs_.clear();
    animations_.clear();
    rigAssetCache_.clear();
    clipViewCache_.clear();
    animGraphCache_.clear();
    materialSetLayout_.reset();
    // bindlessTables_ (layout/pool/SSBO) destructs after this body, once every
    // cache above has released its GPU objects — same order as before.
}

void ResourceManager::setRegistry(AssetRegistry* registry) {
    registry_ = registry;
    assetLoader_->setRegistry(registry);
}

uint32_t ResourceManager::ensureBindlessTextureIndex(Texture* texture) {
    return bindlessTables_.ensureTextureIndex(texture);
}

uint32_t ResourceManager::registerMaterialData(const glm::vec4& baseColor, const glm::vec4& emissive,
                                               float metallic, float roughness, float ao,
                                               uint32_t albedoIdx, uint32_t normalIdx, uint32_t mrIdx,
                                               uint32_t emissiveIdx, MaterialType type) {
    return bindlessTables_.allocMaterialSlot(baseColor, emissive, metallic, roughness, ao,
                                             albedoIdx, normalIdx, mrIdx, emissiveIdx, type);
}

void ResourceManager::updateMaterialData(uint32_t index, const glm::vec4& baseColor,
                                         const glm::vec4& emissive,
                                         float metallic, float roughness, float ao,
                                         uint32_t albedoIdx, uint32_t normalIdx, uint32_t mrIdx,
                                         uint32_t emissiveIdx, MaterialType type) {
    bindlessTables_.writeMaterialSlot(index, baseColor, emissive, metallic, roughness, ao,
                                      albedoIdx, normalIdx, mrIdx, emissiveIdx, type);
}

Mesh* ResourceManager::loadMesh(AssetID id) {
    return meshCache_->load(id, registry_, *assetLoader_);
}

Mesh* ResourceManager::getMesh(AssetID id) {
    return meshCache_->get(id, registry_, *assetLoader_, frameClock_);
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
    if (auto it = textures_.find(id); it != textures_.end()) {
        textureLastUse_[id] = frameClock_;
        return it->second.get();
    }
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
                textureResidentBytes_ += tex->gpuBytes();
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
    const size_t meshesBefore = meshCache_->size();
    const size_t materialsBefore = materials_.size();

    // Textures : tout ce qu'aucun matériau/nœud/skybox vivant ne référence.
    for (auto it = textures_.begin(); it != textures_.end();) {
        if (used.textures.count(it->first)) {
            ++it;
            continue;
        }
        Retired r;
        r.bindlessIndex = it->second->bindlessIndex();
        textureResidentBytes_ -= std::min(textureResidentBytes_, it->second->gpuBytes());
        textureLastUse_.erase(it->first);
        r.texture = std::move(it->second);
        graveyard_.retire(std::move(r), frameClock_);
        it = textures_.erase(it);
    }

    meshCache_->sweepUnused(used.meshes, graveyard_, frameClock_);

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
        graveyard_.retire(std::move(r), frameClock_);
        it = materials_.erase(it);
    }

    // Assets d'animation : rigs et clips que plus aucun Animator vivant ne
    // détient (pointeurs bruts marqués par le collecteur) sont libérés; les
    // caches ClipView/AnimGraph sont de purs caches fichier→asset rechargés
    // par chemin à la demande, balayés entièrement.
    const size_t rigsBefore = rigs_.size();
    const size_t animationsBefore = animations_.size();
    for (auto it = rigs_.begin(); it != rigs_.end();) {
        if (used.rigs.count(it->second.get())) ++it;
        else it = rigs_.erase(it);
    }
    for (auto it = animations_.begin(); it != animations_.end();) {
        if (used.animations.count(it->second.get())) ++it;
        else it = animations_.erase(it);
    }
    const size_t rigAssetsSwept = rigAssetCache_.size();
    const size_t viewsSwept = clipViewCache_.size();
    const size_t graphsSwept = animGraphCache_.size();
    rigAssetCache_.clear();
    clipViewCache_.clear();
    animGraphCache_.clear();

    const size_t evicted = (texturesBefore - textures_.size()) +
                           (meshesBefore - meshCache_->size()) +
                           (materialsBefore - materials_.size()) +
                           (rigsBefore - rigs_.size()) +
                           (animationsBefore - animations_.size()) +
                           rigAssetsSwept + viewsSwept + graphsSwept;
    if (evicted)
        Log::info("assets: evicted ", texturesBefore - textures_.size(), " textures, ",
                  meshesBefore - meshCache_->size(), " meshes, ",
                  materialsBefore - materials_.size(), " materials, ",
                  rigsBefore - rigs_.size(), " rigs, ",
                  animationsBefore - animations_.size(), " clips, ",
                  rigAssetsSwept + viewsSwept + graphsSwept,
                  " rig-assets/views/graphs (gpu resident ",
                  gpuResidentBytes(), " bytes)");
}

void ResourceManager::retireBindGroup(std::unique_ptr<rhi::BindGroup> group) {
    graveyard_.retireBindGroup(std::move(group), frameClock_);
}

void ResourceManager::pumpAssetLoads() {
    ++frameClock_;
    graveyard_.drain(frameClock_, bindlessTables_,
                     bindlessTables_.active() ? defaultWhiteTexture() : nullptr);
    // pump() d'abord : sur le web (pas de worker) c'est lui qui exécute les
    // chargements — finaliser ensuite rend les assets prêts dès cette frame.
    assetLoader_->pump();
    finalizePendingTextures();
    meshCache_->finalizePending(registry_);
    finalizePendingAnimationAssets();
    enforceGpuBudget();
    SAIDA_PROFILE_COUNTER("Assets/GpuResidentBytes", gpuResidentBytes());
    SAIDA_PROFILE_COUNTER("Assets/GpuEvictions", gpuEvictedCount_);
}

void ResourceManager::enforceGpuBudget() {
    if (gpuBudgetBytes_ == 0 || gpuResidentBytes() <= gpuBudgetBytes_) {
        overBudgetWarned_ = false;
        return;
    }
    if (!hasLiveUsage_) return;  // pas encore de photographie : ne rien casser

    // Candidats : assets chargés par id, ni référencés par la scène vivante ni
    // en cours de chargement, ni builtin/défauts. Tri LRU global.
    struct Candidate {
        AssetID id;
        uint64_t lastUse;
        bool isMesh;
    };
    std::vector<Candidate> candidates;
    for (const auto& [id, tex] : textures_) {
        (void)tex;
        if (liveUsage_.textures.count(id) || pendingTextures_.count(id)) continue;
        const auto it = textureLastUse_.find(id);
        candidates.push_back(
            {id, it != textureLastUse_.end() ? it->second : 0, false});
    }
    std::vector<MeshCache::EvictionCandidate> meshCandidates;
    meshCache_->collectEvictionCandidates(liveUsage_.meshes, meshCandidates);
    for (const MeshCache::EvictionCandidate& candidate : meshCandidates)
        candidates.push_back({candidate.id, candidate.lastUse, true});
    std::sort(candidates.begin(), candidates.end(),
              [](const Candidate& a, const Candidate& b) { return a.lastUse < b.lastUse; });

    for (const Candidate& c : candidates) {
        if (gpuResidentBytes() <= gpuBudgetBytes_) break;
        uint64_t bytes = 0;
        if (c.isMesh) {
            bytes = meshCache_->evict(c.id, graveyard_, frameClock_);
        } else {
            auto it = textures_.find(c.id);
            if (it == textures_.end()) continue;
            bytes = it->second->gpuBytes();
            Retired retired;
            retired.bindlessIndex = it->second->bindlessIndex();
            retired.texture = std::move(it->second);
            textures_.erase(it);
            textureResidentBytes_ -= std::min(textureResidentBytes_, bytes);
            textureLastUse_.erase(c.id);
            graveyard_.retire(std::move(retired), frameClock_);
        }
        if (bytes == 0) continue;
        gpuEvictedBytes_ += bytes;
        ++gpuEvictedCount_;
        Log::info("assets: gpu budget evicted ", c.isMesh ? "mesh" : "texture", " id=",
                  c.id, " (", bytes, " bytes, resident ", gpuResidentBytes(), "/",
                  gpuBudgetBytes_, ")");
    }

    if (gpuResidentBytes() > gpuBudgetBytes_ && !overBudgetWarned_) {
        // Tout le dépassement est référencé par la scène : mesuré et signalé
        // une fois, jamais cassé.
        overBudgetWarned_ = true;
        Log::warn("assets: gpu budget exceeded by live content (resident ",
                  gpuResidentBytes(), " > budget ", gpuBudgetBytes_,
                  "), nothing evictable");
    }
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
    textureResidentBytes_ += tex->gpuBytes();

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
    textureResidentBytes_ += tex->gpuBytes();

    static std::atomic<AssetID> s_generatedId{0x8100000000000000ULL};
    AssetID id = s_generatedId++;
    textures_.emplace(id, std::move(tex));
    return id;
}

AssetID ResourceManager::registerMemoryMesh(const std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices) {
    return meshCache_->registerMemory(vertices, indices);
}

AssetID ResourceManager::registerMemoryMesh(const std::string& subPath,
                                            const std::vector<Vertex>& vertices,
                                            const std::vector<uint32_t>& indices) {
    return meshCache_->registerMemory(subPath, vertices, indices, registry_);
}

AssetID ResourceManager::loadRigAsset(const std::string& path) {
    return rigAssetCache_.load(registry_, *assetLoader_, path);
}

const RigAsset* ResourceManager::getRigAsset(AssetID id) const {
    return rigAssetCache_.get(id);
}

AssetLoadState ResourceManager::rigAssetLoadState(AssetID id) const {
    return rigAssetCache_.loadState(id);
}

std::string ResourceManager::rigAssetLoadError(AssetID id) const {
    return rigAssetCache_.loadError(id);
}

AssetID ResourceManager::loadClipView(const std::string& path) {
    return clipViewCache_.load(registry_, *assetLoader_, path);
}

const ClipView* ResourceManager::getClipView(AssetID id) const {
    return clipViewCache_.get(id);
}

AssetLoadState ResourceManager::clipViewLoadState(AssetID id) const {
    return clipViewCache_.loadState(id);
}

std::string ResourceManager::clipViewLoadError(AssetID id) const {
    return clipViewCache_.loadError(id);
}

AssetID ResourceManager::loadAnimGraph(const std::string& path) {
    return animGraphCache_.load(registry_, *assetLoader_, path);
}

const AnimGraphAsset* ResourceManager::getAnimGraph(AssetID id) const {
    return animGraphCache_.get(id);
}

AssetLoadState ResourceManager::animGraphLoadState(AssetID id) const {
    return animGraphCache_.loadState(id);
}

std::string ResourceManager::animGraphLoadError(AssetID id) const {
    return animGraphCache_.loadError(id);
}

void ResourceManager::finalizePendingAnimationAssets() {
    rigAssetCache_.finalize(registry_);
    clipViewCache_.finalize(registry_);
    animGraphCache_.finalize(registry_);
}

AssetID ResourceManager::registerMemoryRig(const std::string& path, std::unique_ptr<Rig> rig) {
    AssetID id = registry_ ? registry_->registerAsset(path, AssetType::Rig) : reinterpret_cast<AssetID>(rig.get());
    // Idempotent : ré-importer le même asset garde l'instance existante — les
    // Animators déjà attachés en détiennent des pointeurs bruts, remplacer
    // l'instance les laisserait pendre (use-after-free au premier update).
    if (rigs_.find(id) == rigs_.end()) rigs_[id] = std::move(rig);
    return id;
}

AssetID ResourceManager::registerMemoryAnimation(const std::string& subPath, std::unique_ptr<AnimationClip> clip) {
    AssetID id = registry_ ? registry_->registerAsset(subPath, AssetType::Animation) : reinterpret_cast<AssetID>(clip.get());
    // Même règle d'idempotence que registerMemoryRig (bibliothèques de clips).
    if (animations_.find(id) == animations_.end()) animations_[id] = std::move(clip);
    return id;
}

AssetID ResourceManager::animationId(const AnimationClip* clip) const {
    for (const auto& [id, owned] : animations_)
        if (owned.get() == clip) return id;
    return kAssetInvalid;
}

AssetID ResourceManager::meshId(const Mesh* mesh) const {
    return meshCache_->idFor(mesh);
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
