#pragma once

#include "render/RenderFeature.hpp"
#include "graphics/Pipeline.hpp"
#include "project/AssetRegistry.hpp"

#include <glm/glm.hpp>

#include <memory>

namespace ne {

// Equirectangular skybox. Owns its own descriptor set (the environment texture)
// and draws a fullscreen triangle at the far plane. Handles both the mono (desktop)
// and stereo (XR multiview, per-eye via gl_ViewIndex) paths; in XR passthrough the
// sky is skipped so the real world shows through.
class SkyboxFeature : public ScenePassFeature {
public:
    ~SkyboxFeature() override;
    void createPipelines(const RenderContext& ctx) override;
    void record(const FrameContext& fc) override;

private:
    struct MonoPush { glm::mat4 invViewProj; float exposure; float rotation; };
    struct StereoPush { glm::mat4 invViewProj[2]; float exposure; float rotation; };

    VulkanDevice* device_ = nullptr;
    ResourceManager* resources_ = nullptr;
    bool stereo_ = false;

    std::unique_ptr<Pipeline> pipeline_;
    VkDescriptorSetLayout setLayout_ = VK_NULL_HANDLE;
    VkDescriptorPool pool_ = VK_NULL_HANDLE;
    VkDescriptorSet set_ = VK_NULL_HANDLE;
    AssetID currentTexture_ = kAssetInvalid;
};

} // namespace ne
