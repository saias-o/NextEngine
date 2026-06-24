#include "render/features/SkyboxFeature.hpp"

#include "core/Paths.hpp"
#include "core/Camera.hpp"
#include "graphics/VulkanDevice.hpp"
#include "graphics/ResourceManager.hpp"
#include "graphics/Texture.hpp"
#include "scene/Scene.hpp"

#include <algorithm>
#include <array>
#include <stdexcept>

namespace ne {

SkyboxFeature::~SkyboxFeature() {
    if (!device_) return;
    if (pool_) vkDestroyDescriptorPool(device_->device(), pool_, nullptr);
    if (setLayout_) vkDestroyDescriptorSetLayout(device_->device(), setLayout_, nullptr);
}

void SkyboxFeature::createPipelines(const RenderContext& ctx) {
    device_ = &ctx.device;
    resources_ = &ctx.resources;
    stereo_ = ctx.stereo();

    VkDescriptorSetLayoutBinding samplerBinding{};
    samplerBinding.binding = 0;
    samplerBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    samplerBinding.descriptorCount = 1;
    samplerBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = 1;
    layoutInfo.pBindings = &samplerBinding;
    if (vkCreateDescriptorSetLayout(device_->device(), &layoutInfo, nullptr, &setLayout_) != VK_SUCCESS)
        throw std::runtime_error("failed to create skybox descriptor set layout");

    VkDescriptorPoolSize poolSize{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1};
    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = &poolSize;
    poolInfo.maxSets = 1;
    if (vkCreateDescriptorPool(device_->device(), &poolInfo, nullptr, &pool_) != VK_SUCCESS)
        throw std::runtime_error("failed to create skybox descriptor pool");

    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = pool_;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &setLayout_;
    if (vkAllocateDescriptorSets(device_->device(), &allocInfo, &set_) != VK_SUCCESS)
        throw std::runtime_error("failed to allocate skybox descriptor set");

    std::vector<VkDescriptorSetLayout> setLayouts = {setLayout_};
    std::vector<VkFormat> colorFormats = {ctx.colorFormat};
    // Depth test on (LEQUAL — sky is at z=1), no depth write, two-sided.
    const char* frag = stereo_ ? "multiview.skybox.frag.spv" : "skybox.frag.spv";
    const uint32_t pushSize = stereo_ ? sizeof(StereoPush) : sizeof(MonoPush);
    pipeline_ = std::make_unique<Pipeline>(ctx.device,
        shaderPath("skybox.vert.spv"), shaderPath(frag),
        colorFormats, ctx.depthFormat, setLayouts, ctx.samples,
        false, true, pushSize, false, VK_COMPARE_OP_LESS_OR_EQUAL, VK_CULL_MODE_NONE,
        false, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, ctx.viewMask);
}

void SkyboxFeature::record(const FrameContext& fc) {
    if (fc.passthrough) return;  // XR see-through: an opaque sky would hide the world

    const auto& settings = fc.scene.settings();
    if (settings.skyboxTexture == kAssetInvalid) return;
    Texture* tex = resources_->getTexture(settings.skyboxTexture);
    if (!tex) return;

    if (settings.skyboxTexture != currentTexture_) {
        VkDescriptorImageInfo imageInfo{};
        imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        imageInfo.imageView = tex->imageView();
        imageInfo.sampler = tex->sampler();
        VkWriteDescriptorSet write{};
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet = set_;
        write.dstBinding = 0;
        write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        write.descriptorCount = 1;
        write.pImageInfo = &imageInfo;
        vkUpdateDescriptorSets(device_->device(), 1, &write, 0, nullptr);
        currentTexture_ = settings.skyboxTexture;
    }

    pipeline_->bind(fc.cmd);
    vkCmdBindDescriptorSets(fc.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_->layout(),
        0, 1, &set_, 0, nullptr);

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

} // namespace ne
