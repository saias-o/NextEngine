#pragma once

#ifndef SAIDA_RHI_WEBGPU
#include <vulkan/vulkan.h>
#endif

#include <glm/glm.hpp>

#include <memory>
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>

#include "project/AssetRegistry.hpp"
#include "project/AssetLoader.hpp"
#include "graphics/Material.hpp"
#include "graphics/GeometryRegistry.hpp"
#include "graphics/AsyncAssetCache.hpp"
#include "graphics/BindlessTables.hpp"
#include "graphics/GpuGraveyard.hpp"
#include "graphics/MeshCache.hpp"
#include "graphics/TextureCache.hpp"
#include "rhi/Rhi.hpp"

#ifdef SAIDA_RHI_WEBGPU
#include "graphics/Buffer.hpp"
#include "graphics/Texture.hpp"
#endif

namespace saida {

class VulkanDevice;
class Mesh;
#ifndef SAIDA_RHI_WEBGPU
class Texture;
#endif
struct Vertex;
class Rig;
class AnimationClip;
class ClipView;
class AnimGraphAsset;
class RigAsset;

// Loads and caches GPU resources. Owns material set 1 layout/pool.
class ResourceManager {
public:
    static constexpr uint32_t kMaxBindlessTextures = 8192;
    static constexpr uint32_t kMaxBindlessMaterials = 4096;
    ResourceManager(rhi::Device& device, AssetRegistry* registry = nullptr);
    ~ResourceManager();
    ResourceManager(const ResourceManager&) = delete;
    ResourceManager& operator=(const ResourceManager&) = delete;

    rhi::Device& device() { return device_; }

    // Mesh by id: a built-in primitive or an .obj AssetID.
    Mesh* getMesh(AssetID id);
    // Texture par id, non bloquante : retourne la
    // texture si elle est résidente, sinon lance le chargement asynchrone
    // (lecture + décodage stbi sur le worker) et retourne nullptr — l'appelant
    // retombe sur ses fallbacks et les matériaux sont rebindés quand la
    // texture devient prête. Un asset en échec rend la texture « missing »
    // (damier magenta), jamais nullptr en boucle.
    Texture* getTexture(AssetID id, bool srgb = true);
    Material* getMaterial(const MaterialDesc& desc);
    Rig* getRig(AssetID id);
    AnimationClip* getAnimation(AssetID id);

    // The id a mesh was loaded with (for serialization).
    AssetID meshId(const Mesh* mesh) const;

    // The id a clip was registered with — its sub-asset key ("model.glb#Run")
    // via the registry. kAssetInvalid for clips not registered here.
    AssetID animationId(const AnimationClip* clip) const;

    AssetRegistry* registry() const { return registry_; }

    // Direct .obj load (e.g. heavy models). getMesh() delegates here for paths.
    Mesh* loadMesh(AssetID id);

    // Register a path dynamically (e.g. for hardcoded demo scenes without pre-sync)
    AssetID getOrRegister(const std::string& path, AssetType type = AssetType::Unknown, bool srgb = true);

    AssetID registerMemoryTexture(const uint8_t* data, size_t size, bool srgb = true);
    AssetID registerGeneratedTexture(const uint8_t* pixels, uint32_t width, uint32_t height,
                                     rhi::Format format = rhi::Format::RGBA8Srgb,
                                     bool generateMipmaps = true);

    // Register a dynamically generated mesh (e.g. from gltf primitive)
    AssetID registerMemoryMesh(const std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices);
    // Saveur à identité stable par clé de sous-asset ("model.gltf#mesh2_prim0"),
    // idempotente comme registerMemoryRig — un ré-import rend le même id.
    AssetID registerMemoryMesh(const std::string& subPath, const std::vector<Vertex>& vertices,
                               const std::vector<uint32_t>& indices);

    // Register memory structures directly from loaders
    AssetID registerMemoryRig(const std::string& path, std::unique_ptr<Rig> rig);
    AssetID registerMemoryAnimation(const std::string& subPath, std::unique_ptr<AnimationClip> clip);

    // Assets d'authoring animation autonomes, cachés par AssetID. Ces appels
    // enregistrent l'identité et lancent une requête asynchrone. Le getter
    // reste null jusqu'à Ready; les erreurs de lecture/parse sont exposées par
    // l'état et le diagnostic sans bloquer le consommateur.
    AssetID loadRigAsset(const std::string& path);
    const RigAsset* getRigAsset(AssetID id) const;
    AssetLoadState rigAssetLoadState(AssetID id) const;
    std::string rigAssetLoadError(AssetID id) const;

    AssetID loadClipView(const std::string& path);
    const ClipView* getClipView(AssetID id) const;
    AssetLoadState clipViewLoadState(AssetID id) const;
    std::string clipViewLoadError(AssetID id) const;

    AssetID loadAnimGraph(const std::string& path);
    const AnimGraphAsset* getAnimGraph(AssetID id) const;
    AssetLoadState animGraphLoadState(AssetID id) const;
    std::string animGraphLoadError(AssetID id) const;

    Texture* defaultWhiteTexture();
    Texture* defaultNormalTexture();
    // Fallback visible d'un asset manquant/corrompu : damier magenta 2x2.
    Texture* missingTexture();

    rhi::BindGroupLayout& materialSetLayout() const { return *materialSetLayout_; }

    // Global bindless texture/material tables (owned by BindlessTables).
    // Pipelines choose the set index.
#ifndef SAIDA_RHI_WEBGPU
    VkDescriptorSetLayout globalMaterialSetLayout() const { return bindlessTables_.layout(); }
    VkDescriptorSet globalMaterialSet() const { return bindlessTables_.set(); }
#endif
    Buffer* globalMaterialBuffer() const { return bindlessTables_.materialBuffer(); }
    
    GeometryRegistry& geometry() { return *geometryRegistry_; }

    void setRegistry(AssetRegistry* registry);
    AssetRegistry* getRegistry() const { return registry_; }
    AssetLoader& assetLoader() { return *assetLoader_; }
    const AssetLoader& assetLoader() const { return *assetLoader_; }
    // Tick par frame : avance l'horloge de rétention, draine le graveyard
    // GPU, finalise les chargements async prêts (création GPU + rebind des
    // matériaux) puis pompe l'AssetLoader.
    void pumpAssetLoads();

    // Octets GPU des ressources résidentes chargées par asset (textures,
    // meshes) — diagnostics de fuite du chantier 3, exposé via assets.stats().
    uint64_t gpuResidentBytes() const {
        return textureCache_->residentBytes() + meshCache_->residentBytes();
    }

    // Budget GPU appliqué PENDANT une scène (pas seulement au changeScene) :
    // au-delà du budget, les assets ni référencés par la scène vivante (cf.
    // setLiveUsage) ni en cours de chargement sont évincés du moins récemment
    // utilisé au plus récent. Si tout le dépassement est référencé, rien n'est
    // cassé : un warning unique le mesure. 0 = illimité.
    void setGpuBudget(uint64_t bytes) { gpuBudgetBytes_ = bytes; }
    uint64_t gpuBudgetBytes() const { return gpuBudgetBytes_; }
    uint64_t gpuEvictedCount() const { return gpuEvictedCount_; }
    uint64_t gpuEvictedBytes() const { return gpuEvictedBytes_; }


    // Ensemble des ressources encore référencées par les scènes vivantes —
    // construit par le SceneTree (walk du World) après un changeScene.
    struct AssetUsage {
        std::unordered_set<const Mesh*> meshes;
        std::unordered_set<AssetID> textures;
        std::unordered_set<const Material*> materials;
        // Animation : rigs et clips encore détenus (pointeurs bruts) par des
        // Animators vivants. Les caches ClipView/AnimGraph n'ont que des
        // consommateurs transitoires (rebind par chemin) et sont balayés
        // entièrement au trim.
        std::unordered_set<const Rig*> rigs;
        std::unordered_set<const AnimationClip*> animations;
    };

    // Évince du cache tout ce
    // que `used` ne référence plus (mark-and-sweep au changement de scène).
    // Les objets GPU partent au graveyard et sont détruits kRetireFrames plus
    // tard (une frame en vol peut encore les lire) ; leurs index bindless et
    // slots matériaux sont alors recyclés. Les builtins, les textures par
    // défaut et les proxies en cours de chargement sont exempts.
    void trimUnused(const AssetUsage& used);

    // Photographie des références vivantes (SceneTree, rafraîchie à chaque
    // changement de hiérarchie) — les candidats à l'éviction budget mi-scène
    // sont exactement les assets hors de cet ensemble.
    void setLiveUsage(AssetUsage usage) {
        liveUsage_ = std::move(usage);
        hasLiveUsage_ = true;
    }

    // Confie un objet GPU potentiellement encore référencé par une frame en
    // vol ; il sera détruit après kRetireFrames pumps (pattern ThumbnailCache).
    void retireBindGroup(std::unique_ptr<rhi::BindGroup> group);

    // Réécrit le slot MaterialData d'un matériau déjà enregistré (rebind après
    // chargement async d'une de ses textures).
    void updateMaterialData(uint32_t index, const glm::vec4& baseColor, const glm::vec4& emissive,
                            float metallic, float roughness, float ao,
                            uint32_t albedoIdx, uint32_t normalIdx, uint32_t mrIdx, uint32_t emissiveIdx,
                            MaterialType type);

    // Register a texture in the bindless array if needed, returns its index.
    uint32_t ensureBindlessTextureIndex(Texture* texture);
    
    // Register material data in the global SSBO, returns its index.
    uint32_t registerMaterialData(const glm::vec4& baseColor, const glm::vec4& emissive,
                                  float metallic, float roughness, float ao,
                                  uint32_t albedoIdx, uint32_t normalIdx, uint32_t mrIdx, uint32_t emissiveIdx,
                                  MaterialType type);

private:
    void finalizePendingAnimationAssets();
    void rebindMaterialsUsing(AssetID textureId);

    rhi::Device& device_;
    AssetRegistry* registry_;
    std::unique_ptr<rhi::BindGroupLayout> materialSetLayout_;

    // Global bindless descriptor tables (texture array + material SSBO).
    BindlessTables bindlessTables_;

    std::unique_ptr<GeometryRegistry> geometryRegistry_;
    std::unique_ptr<MeshCache> meshCache_;
    std::unique_ptr<TextureCache> textureCache_;

    std::unordered_map<MaterialDesc, std::unique_ptr<Material>> materials_;
    std::unordered_map<AssetID, std::unique_ptr<Rig>> rigs_;
    std::unordered_map<AssetID, std::unique_ptr<AnimationClip>> animations_;
    // Standalone authoring animation assets, cached by AssetID via the shared
    // async cache (identical load/get/state/error/finalize for all three).
    AsyncAssetCache<RigAsset> rigAssetCache_;
    AsyncAssetCache<ClipView> clipViewCache_;
    AsyncAssetCache<AnimGraphAsset> animGraphCache_;
    
    std::unique_ptr<AssetLoader> assetLoader_;

    // Objets GPU retirés mais possiblement encore lus par une frame en vol :
    // détruits (et leurs slots bindless recyclés) après un délai (GpuGraveyard).
    GpuGraveyard graveyard_;
    uint64_t frameClock_ = 0;

    // Budget GPU mi-scène. Chaque cache possède son sous-total et son LRU ;
    // ResourceManager fusionne encore leurs candidats jusqu'au prochain lot.
    void enforceGpuBudget();
    uint64_t gpuBudgetBytes_ = 512ull * 1024ull * 1024ull;
    uint64_t gpuEvictedCount_ = 0;
    uint64_t gpuEvictedBytes_ = 0;
    bool overBudgetWarned_ = false;
    AssetUsage liveUsage_;
    bool hasLiveUsage_ = false;
};

} // namespace saida
