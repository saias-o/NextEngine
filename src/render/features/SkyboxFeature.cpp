#include "render/features/SkyboxFeature.hpp"

#include "core/Paths.hpp"
#include "core/Camera.hpp"
#include "graphics/VulkanDevice.hpp"
#include "graphics/ResourceManager.hpp"
#include "graphics/Texture.hpp"
#include "rhi/vulkan/Format.hpp"
#include "scene/Scene.hpp"

#include <algorithm>
#include <array>
#include <stdexcept>

namespace saida {

SkyboxFeature::~SkyboxFeature() = default;

void SkyboxFeature::createPipelines(const RenderContext& ctx) {
    device_ = &ctx.device;
    resources_ = &ctx.resources;
    stereo_ = ctx.stereo();

    setLayout_ = std::make_unique<rhi::BindGroupLayout>(*device_,
        std::vector<rhi::BindGroupLayoutEntry>{
            {0, rhi::BindingType::CombinedImageSampler, rhi::ShaderStages::Fragment},
        });

    // Depth test on (LEQUAL — sky is at z=1), no depth write, two-sided.
    const char* frag = stereo_ ? "multiview.skybox.frag.spv" : "skybox.frag.spv";
    const uint32_t pushSize = stereo_ ? sizeof(StereoPush) : sizeof(MonoPush);
    Pipeline::Desc desc;
    desc.vertPath = shaderPath("skybox.vert.spv");
    desc.fragPath = shaderPath(frag);
    desc.colorFormats = {rhi::vulkan::fromVk(ctx.colorFormat)};
    desc.depthFormat = rhi::vulkan::fromVk(ctx.depthFormat);
    desc.bindGroupLayouts = {setLayout_.get()};
    desc.samples = static_cast<uint32_t>(ctx.samples);
    desc.vertexInput = false;
    desc.depthWrite = false;
    desc.depthCompare = rhi::CompareOp::LessOrEqual;
    desc.cullMode = rhi::CullMode::None;
    desc.pushConstantSize = pushSize;
    desc.viewMask = ctx.viewMask;
    pipeline_ = std::make_unique<Pipeline>(ctx.device, desc);
}

void SkyboxFeature::record(const FrameContext& fc) {
    if (fc.passthrough) return;  // XR see-through: an opaque sky would hide the world

    const auto& settings = fc.scene.settings();
    if (settings.skyboxTexture == kAssetInvalid) return;
    Texture* tex = resources_->getTexture(settings.skyboxTexture);
    if (!tex) return;

    if (settings.skyboxTexture != currentTexture_ || !set_) {
        rhi::BindGroupEntry entry;
        entry.binding = 0;
        entry.view = tex->imageView();
        entry.sampler = tex->sampler();
        set_ = std::make_unique<rhi::BindGroup>(*setLayout_, std::vector<rhi::BindGroupEntry>{entry});
        currentTexture_ = settings.skyboxTexture;
    }

    pipeline_->bind(fc.cmd);
    VkDescriptorSet setHandle = set_->handle();
    vkCmdBindDescriptorSets(fc.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_->layout(),
        0, 1, &setHandle, 0, nullptr);

    if (!stereo_) {
        glm::mat4 view = fc.camera->view();
        view[3] = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);  // strip translation
        MonoPush pc{};
        pc.invViewProj = glm::inverse(fc.camera->projection() * view);
        pc.exposure = settings.skyboxExposure;
        pc.rotation = settings.skyboxRotation;
        vkCmdPushConstants(fc.cmd, pipeline_->layout(),
            VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(MonoPush), &pc);
    } else {
        const auto& eyes = *fc.eyes;
        StereoPush pc{};
        const uint32_t n = std::min<uint32_t>(static_cast<uint32_t>(eyes.size()), 2);
        for (uint32_t i = 0; i < n; ++i) {
            glm::mat4 view = eyes[i].view;
            view[3] = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
            pc.invViewProj[i] = glm::inverse(eyes[i].projection * view);
        }
        if (n == 1) pc.invViewProj[1] = pc.invViewProj[0];
        pc.exposure = settings.skyboxExposure;
        pc.rotation = settings.skyboxRotation;
        vkCmdPushConstants(fc.cmd, pipeline_->layout(),
            VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(StereoPush), &pc);
    }

    vkCmdDraw(fc.cmd, 3, 1, 0, 0);
}

} // namespace saida
