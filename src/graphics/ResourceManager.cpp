#include "graphics/ResourceManager.hpp"

#include "core/Log.hpp"
#include "core/Profiler.hpp"
#include "graphics/Material.hpp"
#include "project/AssetRegistry.hpp"
#include "scene/animation/Rig.hpp"
#include "scene/animation/RigAsset.hpp"
#include "scene/animation/AnimationClip.hpp"
#include "scene/animation/AnimationAssetDecoders.hpp"
#include "scene/animation/AnimGraphAsset.hpp"
#include "scene/animation/ClipView.hpp"

#include <algorithm>
#include <string>

namespace saida {

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
    textureCache_ = std::make_unique<TextureCache>(device_, bindlessTables_);
}

ResourceManager::~ResourceManager() {
    assetLoader_.reset();
    // Clear caches first (resource destructors run while the device is alive),
    // then the descriptor objects they were allocated from. The graveyard goes
    // with them (retired bind groups were allocated from the same layout/pool).
    graveyard_.clear();
    materials_.clear();
    textureCache_->clear();
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
    return textureCache_->get(id, srgb, registry_, *assetLoader_, frameClock_);
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
    const size_t texturesBefore = textureCache_->size();
    const size_t meshesBefore = meshCache_->size();
    const size_t materialsBefore = materials_.size();

    textureCache_->sweepUnused(used.textures, graveyard_, frameClock_);
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

    const size_t evicted = (texturesBefore - textureCache_->size()) +
                           (meshesBefore - meshCache_->size()) +
                           (materialsBefore - materials_.size()) +
                           (rigsBefore - rigs_.size()) +
                           (animationsBefore - animations_.size()) +
                           rigAssetsSwept + viewsSwept + graphsSwept;
    if (evicted)
        Log::info("assets: evicted ", texturesBefore - textureCache_->size(), " textures, ",
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
    std::vector<AssetID> completedTextures;
    textureCache_->finalizePending(completedTextures);
    for (AssetID id : completedTextures)
        rebindMaterialsUsing(id);
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
    enum class CacheTag {
        Texture,
        Mesh,
    };
    struct Candidate {
        AssetID id;
        uint64_t lastUse;
        CacheTag cache;
    };
    std::vector<Candidate> candidates;
    std::vector<TextureCache::EvictionCandidate> textureCandidates;
    textureCache_->collectEvictionCandidates(liveUsage_.textures, textureCandidates);
    for (const TextureCache::EvictionCandidate& candidate : textureCandidates)
        candidates.push_back({candidate.id, candidate.lastUse, CacheTag::Texture});
    std::vector<MeshCache::EvictionCandidate> meshCandidates;
    meshCache_->collectEvictionCandidates(liveUsage_.meshes, meshCandidates);
    for (const MeshCache::EvictionCandidate& candidate : meshCandidates)
        candidates.push_back({candidate.id, candidate.lastUse, CacheTag::Mesh});
    std::sort(candidates.begin(), candidates.end(),
              [](const Candidate& a, const Candidate& b) { return a.lastUse < b.lastUse; });

    for (const Candidate& c : candidates) {
        if (gpuResidentBytes() <= gpuBudgetBytes_) break;
        const bool isMesh = c.cache == CacheTag::Mesh;
        const uint64_t bytes = isMesh
            ? meshCache_->evict(c.id, graveyard_, frameClock_)
            : textureCache_->evict(c.id, graveyard_, frameClock_);
        if (bytes == 0) continue;
        gpuEvictedBytes_ += bytes;
        ++gpuEvictedCount_;
        Log::info("assets: gpu budget evicted ", isMesh ? "mesh" : "texture", " id=",
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
    return textureCache_->registerMemory(data, size, srgb);
}

AssetID ResourceManager::registerGeneratedTexture(const uint8_t* pixels, uint32_t width,
                                                  uint32_t height, rhi::Format format,
                                                  bool generateMipmaps) {
    return textureCache_->registerGenerated(
        pixels, width, height, format, generateMipmaps);
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

Texture* ResourceManager::defaultWhiteTexture() {
    return textureCache_->defaultWhite();
}

Texture* ResourceManager::defaultNormalTexture() {
    return textureCache_->defaultNormal();
}

Texture* ResourceManager::missingTexture() {
    return textureCache_->missing();
}

} // namespace saida
