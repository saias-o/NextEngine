#pragma once

#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include <glm/glm.hpp>

#include <array>
#include <memory>
#include <vector>

#include "core/Camera.hpp"
#include "project/AssetRegistry.hpp"
#include "graphics/Material.hpp"     // MaterialType
#include "render/RenderFeature.hpp"  // EyeRenderInfo, RenderContext, FrameContext, ScenePassFeature

namespace ne {

class VulkanDevice;
class Swapchain;
class Window;
class Pipeline;
class ComputePipeline;
class Buffer;
class ImGuiLayer;
class Scene;
class Camera;
class Mesh;
class Project;
class Swapchain;
class Material;
class ResourceManager;
class ShadowMap;
class UIRenderer;
class GIVolume;
class GpuProfiler;
class PostProcessor;
struct SceneSettings;

// Camera block shared by the desktop (mono) and XR (stereo multiview) paths:
// view/proj are arrays of 2 (left/right eye). Mono fills/uses index 0; the
// multiview scene shader indexes by gl_ViewIndex. Mirrors CameraUBO in shader.vert.
struct UniformBufferObject {
    alignas(16) glm::mat4 view[2];
    alignas(16) glm::mat4 proj[2];
};

// Per-object scene push constant (vertex + fragment). Mirrors PushConstants in
// shader.vert / shader.frag.
struct PushConstants {
    glm::mat4 model;
    glm::vec4 params;  // y = bone matrix offset, or -1 when unskinned
};

// Limits kept simple and easy to raise; mirrored by the shader constants.
constexpr int kMaxLights = 16;
constexpr int kMaxShadowCasters = 4;

// std140-compatible lighting block (mirrors the LightingUBO in shader.frag).
// One unified light type covers directional / point / spot. Everything is
// vec4/ivec4 to avoid std140 vec3 padding surprises.
struct GpuLight {
    glm::vec4 posRange{0.0f};    // xyz world pos (point/spot), w = range
    glm::vec4 colorInt{0.0f};    // rgb color, w = intensity
    glm::vec4 dirType{0.0f};     // xyz direction (dir/spot), w = type (0 dir,1 point,2 spot)
    glm::vec4 spotShadow{0.0f, 0.0f, -1.0f, 0.0f};  // x cosInner, y cosOuter, z shadowIndex, w unused
};

struct LightingUBO {
    glm::vec4 ambient{0.0f};
    glm::vec4 cameraPos{0.0f};
    glm::vec4 shadowParams; // x = softness, yzw = unused
    glm::ivec4 counts{0};  // x = light count, y = mode (0 realtime, 1 baked)
    GpuLight lights[kMaxLights]{};
    glm::mat4 shadowMatrices[kMaxShadowCasters]{};  // light-space view-proj per shadow caster
    // DDGI irradiance volume params (mirror lighting.glsl). The single GI primitive.
    glm::vec4 giOrigin{0.0f};   // xyz = volume min corner (world), w = enabled (0/1)
    glm::vec4 giSpacing{1.0f};  // xyz = probe spacing
    glm::ivec4 giCounts{0};     // xyz = probe counts, w = probesPerRow in atlas
    glm::ivec4 giAtlas{0};      // x = irradiance texels/probe, y = visibility texels/probe
    glm::vec4 environmentParams{0.0f}; // x enabled, y diffuse, z specular, w rotation
};

struct InstanceData {
    glm::mat4 model;
    glm::vec4 boundingSphere; // xyz = center, w = radius
    uint32_t materialIndex;
    int32_t boneOffset;
    uint32_t pad[2];
};

// Owns the GPU frame machinery: the scene pipeline, set-0 (global) descriptors,
// per-frame uniform buffers, command buffers and sync objects. Each frame it
// updates the camera/lighting UBOs from the scene, records the scene draws plus
// the ImGui overlay, then submits and presents (handling resize). The Engine
// keeps orchestration (window, scene, loop); the Renderer is the only thing that
// touches the swap chain present path — the seam a future XR path slots into.
class Renderer {
public:
    Renderer(VulkanDevice& device, Swapchain& swapchain, Window& window,
             ResourceManager& resources, ImGuiLayer& imgui);
#ifdef NE_ENABLE_XR
    // XR constructor: no window swapchain / ImGui — presentation is owned by the
    // OpenXR session. Builds the multiview (stereo) scene/skybox/tonemap pipelines
    // and the 2-layer render targets sized to one eye. Shares all the rest of the
    // engine's rendering machinery (pipelines, descriptors, shadows, GI).
    Renderer(VulkanDevice& device, Window& window, ResourceManager& resources,
             VkExtent2D xrEyeExtent, VkFormat xrColorFormat, uint32_t xrViewCount);
#endif
    ~Renderer();
    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    // Desktop frame: render the scene and present to the window swapchain.
    void drawFrame(Scene& scene, Camera& camera, Project* project);
    void setViewportRect(glm::vec2 position, glm::vec2 size);
    void clearViewportRect();

#ifdef NE_ENABLE_XR
    void drawXr(VkCommandBuffer cmd, const std::vector<EyeRenderInfo>& eyes,
                Scene& scene, Project* project);
#endif

private:
    struct TonemapPushConstants {
        glm::mat4 invProjection{1.0f};
        glm::vec4 aoParams{0.0f};        // x enabled, y radius, z intensity, w power
        glm::vec4 fogColor{0.0f};
        glm::vec4 fogParams{0.0f};       // x enabled, y start, z density, w exposure
        glm::vec4 bloomParams{0.0f};     // x enabled, y threshold, z intensity, w radius px
        glm::vec4 sourceRect{0.0f, 0.0f, 1.0f, 1.0f};
        glm::vec4 projectionParams{0.0f};
        glm::vec4 projectionParams2{0.0f};
    };

    struct WebCanvasWorldPushConstants {
        glm::mat4 model{1.0f};
        glm::vec4 params{0.0f};  // x width, y height, z bindless texture index, w alpha
    };

    void updateGlobalShadowDescriptor();
    void updateGIDescriptors();  // re-point set 0 bindings 4/5 at the current GI atlas
    void updateEnvironmentDescriptor(Scene& scene);
    bool shouldUpdateRealtimeGI(bool dirty) const;
    uint64_t giDirtySignature(const Scene& scene) const;
    TonemapPushConstants tonemapPushConstants(const SceneSettings& settings,
                                              const glm::mat4& projection) const;
    void createGlobalSetLayout();
    void createPipeline(VkDescriptorSetLayout materialSetLayout);
    void createWebCanvasWorldPipeline();
    // The scene pipeline for a material's shading model (desktop / XR). Both
    // variants share an identical descriptor-set + push-constant layout, so the
    // draw loop can swap between them and keep its bound sets (layout-compatible).
    Pipeline* scenePipelineFor(MaterialType type) const {
        return (type == MaterialType::Unlit && unlitPipeline_) ? unlitPipeline_.get()
                                                               : pipeline_.get();
    }
    void createHdrResources();
    void cleanupHdrResources();
    void createTonemapPipeline();
    void updateTonemapDescriptorSet();
    void recordTonemapPass(VkCommandBuffer cmd, uint32_t imageIndex,
                           Scene& scene, const Camera& camera);
    void createUniformBuffers();
    void createGlobalDescriptorPool();
    void createGlobalDescriptorSets();
    void createCommandBuffers();
    void createSyncObjects();
    void createGpuDrivenBuffers();
    void createCullingPipeline();

    void updateUniformBuffer(uint32_t frame, Scene& scene, Camera& camera, Project* project);
    // Fills the lighting UBO + the per-frame draw list (and bone matrices). Culls
    // against `cullFrustum` when non-null; XR (stereo) passes null to render all.
    void gatherScene(LightingUBO& ubo, Scene& scene, const glm::vec3& cameraPos,
                     const Frustum* cullFrustum, Project* project,
                     const glm::mat4* view = nullptr, const glm::mat4* proj = nullptr);
    // Records the sorted CPU mesh draw list for both desktop and XR. The caller
    // chooses the first pipeline for the active render target; this method handles
    // pipeline switches, material binds, push constants and mesh draws.
    void recordMeshDraws(VkCommandBuffer cmd, Pipeline* firstPipeline, bool xrMultiview);
    void recordWorldWebCanvases(VkCommandBuffer cmd, Scene& scene, const Camera& camera);
    void recordShadowPasses(VkCommandBuffer cmd);
    void recordCommandBuffer(VkCommandBuffer cmd, uint32_t imageIndex, Scene& scene, const Camera& camera);
    VkRect2D activeRenderRect() const;

    VulkanDevice& device_;
    Swapchain* swapchain_ = nullptr;  // null in XR mode (OpenXR owns presentation)
    Window& window_;
    ResourceManager& resources_;
    ImGuiLayer* imgui_ = nullptr;     // null in XR mode (no debug overlay yet)

    std::unique_ptr<Pipeline> pipeline_;        // scene pipeline: MaterialType::Lit
    std::unique_ptr<Pipeline> unlitPipeline_;   // scene pipeline: MaterialType::Unlit
    std::unique_ptr<Pipeline> webCanvasWorldPipeline_;
    std::unique_ptr<ShadowMap> shadowMap_;
    std::unique_ptr<UIRenderer> uiRenderer_;
    std::unique_ptr<GIVolume> gi_;  // DDGI irradiance volume (the single GI primitive)
    std::unique_ptr<GpuProfiler> gpuProfiler_;

    bool viewportOverride_ = false;
    glm::vec2 viewportPos_{0.0f};
    glm::vec2 viewportSize_{0.0f};

    // HDR offscreen target — scene renders here, then tonemapped to swapchain.
    VkImage hdrImage_ = VK_NULL_HANDLE;
    VmaAllocation hdrAllocation_ = VK_NULL_HANDLE;
    VkImageView hdrView_ = VK_NULL_HANDLE;
    VkImage hdrMsaaImage_ = VK_NULL_HANDLE;  // MSAA resolve target (when MSAA on)
    VmaAllocation hdrMsaaAllocation_ = VK_NULL_HANDLE;
    VkImageView hdrMsaaView_ = VK_NULL_HANDLE;
    VkImage depthResolveImage_ = VK_NULL_HANDLE;  // single-sample depth for AO when MSAA is enabled
    VmaAllocation depthResolveAllocation_ = VK_NULL_HANDLE;
    VkImageView depthResolveView_ = VK_NULL_HANDLE;
    uint64_t hdrTrackedBytes_ = 0;
    std::unique_ptr<PostProcessor> postProcessor_;

    // Tonemap pipeline
    std::unique_ptr<Pipeline> tonemapPipeline_;
    VkDescriptorSetLayout tonemapSetLayout_ = VK_NULL_HANDLE;
    VkDescriptorPool tonemapPool_ = VK_NULL_HANDLE;
    VkDescriptorSet tonemapSet_ = VK_NULL_HANDLE;
    VkSampler tonemapSampler_ = VK_NULL_HANDLE;
    VkSampler tonemapDepthSampler_ = VK_NULL_HANDLE;
    float exposure_ = 1.0f;

    // Scene-pass features (skybox, water, debug lines, …). The Renderer builds them
    // once and iterates them after the opaque draws — adding/removing an effect is a
    // new ScenePassFeature, never an edit here. Order = draw order.
    std::vector<std::unique_ptr<ScenePassFeature>> features_;
    void buildFeatures(uint32_t viewMask, VkFormat depthFormat,
                       VkSampleCountFlagBits samples);
    void recordFeatures(const FrameContext& fc);

    VkDescriptorSetLayout globalSetLayout_ = VK_NULL_HANDLE;
    VkDescriptorPool globalPool_ = VK_NULL_HANDLE;
    std::vector<VkDescriptorSet> globalSets_;
    std::vector<std::unique_ptr<Buffer>> uniformBuffers_;
    std::vector<std::unique_ptr<Buffer>> lightingBuffers_;
    std::vector<std::unique_ptr<Buffer>> boneMatricesBuffers_;

    // GPU-driven rendering resources
    std::vector<std::unique_ptr<Buffer>> instanceBuffers_;
    std::vector<std::unique_ptr<Buffer>> originalDrawCommandBuffers_;
    std::vector<std::unique_ptr<Buffer>> drawCommandBuffers_;
    std::vector<std::unique_ptr<Buffer>> countBuffers_;
    static constexpr uint32_t kMaxInstances = 100000;
    
    std::unique_ptr<ComputePipeline> cullingPipeline_;
    VkDescriptorSetLayout cullingSetLayout_ = VK_NULL_HANDLE;
    VkDescriptorPool cullingPool_ = VK_NULL_HANDLE;
    std::vector<VkDescriptorSet> cullingSets_;
    uint32_t currentInstanceCount_ = 0;
    Frustum cameraFrustum_;

    std::vector<VkCommandBuffer> commandBuffers_;
    std::vector<VkSemaphore> imageAvailableSemaphores_;
    std::vector<VkFence> inFlightFences_;
    uint32_t currentFrame_ = 0;
    
    std::vector<SceneDraw> currentDraws_;
    struct ShadowDraw {
        Mesh* mesh = nullptr;
        glm::mat4 world{1.0f};
    };
    std::vector<ShadowDraw> shadowDraws_;

    // Shadow casters collected this frame: light-space view-proj matrices and
    // their count, consumed by the shadow passes recorded before the main pass.
    std::array<glm::mat4, kMaxShadowCasters> shadowMatrices_{};
    int shadowCount_ = 0;

    // DDGI update control. Full realtime updates every frame; amortized realtime
    // warms up then updates on a cadence. Baked mode converges then freezes.
    static constexpr int kGIBakeFrames = 256;
    static constexpr int kGIRealtimeWarmupFrames = 8;
    int giBakeFramesRemaining_ = 0;
    int giRealtimeWarmupRemaining_ = kGIRealtimeWarmupFrames;
    uint64_t giFrameCounter_ = 0;
    uint32_t giLastHierarchyVersion_ = 0;
    uint32_t giLastTransformVersion_ = 0;
    uint64_t giLastDirtySignature_ = 0;
    bool giWasEnabled_ = true;
    int giLastLightingMode_ = 0;
    int giLastMode_ = 0;
    bool giUpdateThisFrame_ = true;
    std::array<VkImageView, 2> cachedGiIrradianceView_{};
    std::array<VkImageView, 2> cachedGiVisibilityView_{};
    std::array<VkSampler, 2> cachedGiSampler_{};
    std::array<VkImageView, 2> cachedEnvironmentView_{};
    std::array<VkSampler, 2> cachedEnvironmentSampler_{};

#ifdef NE_ENABLE_XR
    // Created only by the XR constructor. The scene is rendered once into a
    // 2-layer HDR target (viewMask = 0b11, gl_ViewIndex per eye), then each layer
    // is tonemapped into the matching eye swapchain image.
    bool xrMode_ = false;
    VkExtent2D xrExtent_{};
    VkFormat xrColorFormat_{};       // XR swapchain (sRGB) format
    uint32_t xrViewCount_ = 2;

    void createXrTargets();
    void createXrPipelines();
    void cleanupXrTargets();
    void updateXrTonemapDescriptorSets();
    void updateUniformBufferXr(uint32_t frame, const std::vector<EyeRenderInfo>& eyes,
                               Scene& scene, Project* project);
    void recordXrScenePass(VkCommandBuffer cmd, Scene& scene,
                           const std::vector<EyeRenderInfo>& eyes);
    void recordXrWorldWebCanvases(VkCommandBuffer cmd, Scene& scene,
                                  const std::vector<EyeRenderInfo>& eyes);
    void recordXrTonemap(VkCommandBuffer cmd, Scene& scene,
                         const std::vector<EyeRenderInfo>& eyes);

    std::unique_ptr<Pipeline> xrScenePipeline_;    // multiview scene: MaterialType::Lit
    std::unique_ptr<Pipeline> xrUnlitPipeline_;    // multiview scene: MaterialType::Unlit
    std::unique_ptr<Pipeline> xrWebCanvasWorldPipeline_;
    std::unique_ptr<Pipeline> xrTonemapPipeline_;  // per-eye tonemap → XR image
    // Skybox/water in XR are scene-pass features (see features_), shared with desktop.

    // XR counterpart of scenePipelineFor (multiview pipelines).
    Pipeline* xrScenePipelineFor(MaterialType type) const {
        return (type == MaterialType::Unlit && xrUnlitPipeline_) ? xrUnlitPipeline_.get()
                                                                 : xrScenePipeline_.get();
    }

    // 2-layer HDR color + depth (one layer per eye).
    VkImage xrHdrImage_ = VK_NULL_HANDLE;
    VmaAllocation xrHdrAllocation_ = VK_NULL_HANDLE;
    VkImageView xrHdrArrayView_ = VK_NULL_HANDLE;      // 2D_ARRAY, render target
    std::array<VkImageView, 2> xrHdrLayerViews_{};     // per-layer, tonemap source
    VkImage xrDepthImage_ = VK_NULL_HANDLE;
    VmaAllocation xrDepthAllocation_ = VK_NULL_HANDLE;
    VkImageView xrDepthArrayView_ = VK_NULL_HANDLE;
    std::array<VkImageView, 2> xrDepthLayerViews_{};   // per-layer, tonemap depth source
    uint64_t xrTrackedBytes_ = 0;

    VkDescriptorPool xrTonemapPool_ = VK_NULL_HANDLE;
    std::array<VkDescriptorSet, 2> xrTonemapSets_{};   // one per eye layer
    std::array<std::unique_ptr<PostProcessor>, 2> xrPostProcessors_;
#else
    bool xrMode_ = false; // fallback for non-XR builds
#endif

    glm::mat4 lodView_{1.0f};
    glm::mat4 lodProj_{1.0f};
    bool lodMatricesValid_ = false;
};

} // namespace ne
