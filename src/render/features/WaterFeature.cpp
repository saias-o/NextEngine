#include "render/features/WaterFeature.hpp"

#include "core/Paths.hpp"
#include "graphics/Buffer.hpp"
#include "graphics/VulkanDevice.hpp"
#include "scene/Scene.hpp"
#include "scene/Node.hpp"
#include "scene/WaterNode.hpp"

#include <glm/gtc/constants.hpp>

#include <algorithm>
#include <array>
#include <stdexcept>
#include <vector>

namespace saida {

WaterFeature::~WaterFeature() {
    if (!device_) return;
    if (pool_) vkDestroyDescriptorPool(device_->device(), pool_, nullptr);
    if (setLayout_) vkDestroyDescriptorSetLayout(device_->device(), setLayout_, nullptr);
}

void WaterFeature::createPipelines(const RenderContext& ctx) {
    device_ = &ctx.device;
    const uint32_t frames = std::max(1u, ctx.framesInFlight);

    // set 1: the per-node water UBO array (vertex needs it for waves + shore flatten,
    // fragment for shading), one buffer + set per frame-in-flight so a frame never
    // rewrites data the GPU is still reading.
    VkDescriptorSetLayoutBinding uboBinding{};
    uboBinding.binding = 0;
    uboBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    uboBinding.descriptorCount = 1;
    uboBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = 1;
    layoutInfo.pBindings = &uboBinding;
    if (vkCreateDescriptorSetLayout(device_->device(), &layoutInfo, nullptr, &setLayout_) != VK_SUCCESS)
        throw std::runtime_error("failed to create water descriptor set layout");

    VkDescriptorPoolSize poolSize{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, frames};
    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = &poolSize;
    poolInfo.maxSets = frames;
    if (vkCreateDescriptorPool(device_->device(), &poolInfo, nullptr, &pool_) != VK_SUCCESS)
        throw std::runtime_error("failed to create water descriptor pool");

    ubos_.resize(frames);
    sets_.resize(frames);
    const VkDeviceSize bufSize = sizeof(GpuWater) * kMaxWaters;
    for (uint32_t i = 0; i < frames; ++i) {
        ubos_[i] = std::make_unique<Buffer>(*device_, bufSize,
            VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, MemoryUsage::HostVisible);

        VkDescriptorSetAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocInfo.descriptorPool = pool_;
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts = &setLayout_;
        if (vkAllocateDescriptorSets(device_->device(), &allocInfo, &sets_[i]) != VK_SUCCESS)
            throw std::runtime_error("failed to allocate water descriptor set");

        VkDescriptorBufferInfo bufferInfo{};
        bufferInfo.buffer = ubos_[i]->handle();
        bufferInfo.offset = 0;
        bufferInfo.range = bufSize;
        VkWriteDescriptorSet write{};
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet = sets_[i];
        write.dstBinding = 0;
        write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        write.descriptorCount = 1;
        write.pBufferInfo = &bufferInfo;
        vkUpdateDescriptorSets(device_->device(), 1, &write, 0, nullptr);
    }

    // set 0 = global (camera + lighting + env); set 1 = the water UBO array. Look/feel
    // is data; the per-draw push is just the node index + time. Procedural grid (no
    // vertex input), depth-tested + depth-writing, two-sided, and ALPHA-BLENDED so the
    // shore can dissolve into the wet sand.
    std::vector<VkDescriptorSetLayout> setLayouts = {ctx.globalSetLayout, setLayout_};
    std::vector<VkFormat> colorFormats = {ctx.colorFormat};
    const char* vert = ctx.stereo() ? "multiview.water.vert.spv" : "water.vert.spv";
    pipeline_ = std::make_unique<Pipeline>(ctx.device,
        shaderPath(vert), shaderPath("water.frag.spv"),
        colorFormats, ctx.depthFormat, setLayouts, ctx.samples,
        false, true, sizeof(Push), true, VK_COMPARE_OP_LESS, VK_CULL_MODE_NONE,
        true, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, ctx.viewMask);
}

void WaterFeature::record(const FrameContext& fc) {
    std::array<GpuWater, kMaxWaters> packed{};
    uint32_t waterCount = 0;

    // Pack every water node into this frame's UBO array.
    for (WaterNode* w : fc.scene.waterNodes()) {
        if (!w->isActiveInHierarchy()) continue;
        if (waterCount >= kMaxWaters) break;  // hard cap (UBO array size)

        glm::vec3 c = glm::vec3(w->worldTransform()[3]);
        GpuWater& g = packed[waterCount++];
        g.area = glm::vec4(c.x, c.y, c.z, w->size);
        g.deep = glm::vec4(w->deepColor, w->roughness);
        g.foam = glm::vec4(w->foamColor, w->reflectivity);
        g.waveA = glm::vec4(w->amplitude, w->wavelength, w->waveSpeed, w->choppiness);
        g.detail1 = glm::vec4(w->detailScale, w->detailSpeed, w->detailStrength, w->detailAngle);
        g.detail2 = glm::vec4(w->detail2Scale, w->detail2Speed, w->detail2Strength, w->detail2Angle);
        g.look = glm::vec4(w->fresnelPower, w->specularPower, w->specularIntensity, w->foamThreshold);
        g.misc = glm::vec4(w->warpAmount, w->detailFadeDistance, w->foamIntensity, w->depthColorFalloff);
        g.shoreColor = glm::vec4(w->shallowColor, w->edgeFade);

        const int mode = static_cast<int>(w->shoreMode);
        if (mode == 1) {  // Beach: shoreline direction (inland) + waterline distance + slope.
            const float a = glm::radians(w->shoreAngle);
            g.shoreGeom = glm::vec4(std::cos(a), std::sin(a), w->shoreWaterline, w->shoreSlope);
        } else {          // Lake (and unused for None): centre on the node, radius + slope.
            g.shoreGeom = glm::vec4(c.x, c.z, w->lakeRadius, w->shoreSlope);
        }
        g.shoreTune = glm::vec4(w->foamWidth, w->swashSpeed, w->swashAmount, w->waveFlatten);
        g.shoreMode = glm::vec4(static_cast<float>(mode), w->shoreFoam, 0.0f, 0.0f);
    }
    if (waterCount == 0) return;

    const uint32_t frame = std::min<uint32_t>(fc.frameIndex,
                                              static_cast<uint32_t>(ubos_.size()) - 1);
    ubos_[frame]->write(packed.data(), sizeof(GpuWater) * waterCount);

    pipeline_->bind(fc.cmd);
    VkDescriptorSet bound[2] = {fc.globalSet, sets_[frame]};
    vkCmdBindDescriptorSets(fc.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_->layout(),
        0, 2, bound, 0, nullptr);

    for (uint32_t i = 0; i < waterCount; ++i) {
        Push pc{i, fc.time};
        vkCmdPushConstants(fc.cmd, pipeline_->layout(),
            VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
            0, sizeof(Push), &pc);
        vkCmdDraw(fc.cmd, kGridRes * kGridRes * 6, 1, 0, 0);
    }
}

} // namespace saida
