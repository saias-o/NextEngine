#include "render/PostProcessor.hpp"

#include "core/Paths.hpp"
#include "graphics/GpuProfiler.hpp"
#include "graphics/GpuSync.hpp"
#include "graphics/MemoryProfiler.hpp"
#include "graphics/Pipeline.hpp"
#include "graphics/VulkanDevice.hpp"
#include "rhi/vulkan/Format.hpp"
#include "scene/Scene.hpp"

#include "vk_mem_alloc.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <stdexcept>

namespace saida {

namespace {
constexpr uint32_t kMaxBloomLevels = 6;
constexpr uint32_t kMinBloomSize = 16;

uint64_t targetBytes(VkExtent2D extent) {
    return static_cast<uint64_t>(extent.width) * extent.height * 8u;
}
}

PostProcessor::PostProcessor(VulkanDevice& device, VkExtent2D extent, VkFormat hdrFormat,
                             VkImageView hdrInputView)
    : device_(device), extent_(extent), hdrFormat_(hdrFormat), hdrInputView_(hdrInputView) {
    createTargets();
    createSampler();
    createDescriptorResources();
    createPipelines();
}

PostProcessor::~PostProcessor() {
    bloomUpsamplePipeline_.reset();
    bloomDownsamplePipeline_.reset();
    downsampleGroups_.clear();  // groups return their sets to the layout's pool
    upsampleGroups_.clear();
    inputLayout_.reset();
    if (linearSampler_) vkDestroySampler(device_.device(), linearSampler_, nullptr);
    destroyTargets();
}

void PostProcessor::setHdrInput(VkImageView hdrInputView) {
    if (hdrInputView_ == hdrInputView) return;
    hdrInputView_ = hdrInputView;
    updateDescriptorSets();
}

VkImageView PostProcessor::bloomView() const {
    return bloom_.empty() ? VK_NULL_HANDLE : bloom_.front().view;
}

void PostProcessor::createTargets() {
    uint32_t w = std::max(1u, extent_.width / 2u);
    uint32_t h = std::max(1u, extent_.height / 2u);
    uint64_t bytes = 0;

    while (w >= kMinBloomSize && h >= kMinBloomSize && bloom_.size() < kMaxBloomLevels) {
        Target target{};
        target.extent = {w, h};

        VkImageCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        info.imageType = VK_IMAGE_TYPE_2D;
        info.extent = {w, h, 1};
        info.mipLevels = 1;
        info.arrayLayers = 1;
        info.format = hdrFormat_;
        info.tiling = VK_IMAGE_TILING_OPTIMAL;
        info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        info.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        info.samples = VK_SAMPLE_COUNT_1_BIT;
        info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VmaAllocationCreateInfo alloc{};
        alloc.usage = VMA_MEMORY_USAGE_AUTO;
        if (vmaCreateImage(device_.allocator(), &info, &alloc,
                           &target.image, &target.allocation, nullptr) != VK_SUCCESS) {
            throw std::runtime_error("PostProcessor: failed to create bloom target");
        }
        target.view = device_.createImageView(target.image, hdrFormat_, VK_IMAGE_ASPECT_COLOR_BIT);
        bytes += targetBytes(target.extent);
        bloom_.push_back(target);

        w /= 2u;
        h /= 2u;
    }

    if (bloom_.empty()) {
        Target target{};
        target.extent = {1, 1};
        VkImageCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        info.imageType = VK_IMAGE_TYPE_2D;
        info.extent = {1, 1, 1};
        info.mipLevels = 1;
        info.arrayLayers = 1;
        info.format = hdrFormat_;
        info.tiling = VK_IMAGE_TILING_OPTIMAL;
        info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        info.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        info.samples = VK_SAMPLE_COUNT_1_BIT;
        info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        VmaAllocationCreateInfo alloc{};
        alloc.usage = VMA_MEMORY_USAGE_AUTO;
        if (vmaCreateImage(device_.allocator(), &info, &alloc,
                           &target.image, &target.allocation, nullptr) != VK_SUCCESS) {
            throw std::runtime_error("PostProcessor: failed to create fallback bloom target");
        }
        target.view = device_.createImageView(target.image, hdrFormat_, VK_IMAGE_ASPECT_COLOR_BIT);
        bytes += targetBytes(target.extent);
        bloom_.push_back(target);
    }

    MemoryProfiler::registerAllocation("RenderTarget/PostBloom", bytes);
}

void PostProcessor::destroyTargets() {
    uint64_t bytes = 0;
    for (const Target& target : bloom_) bytes += targetBytes(target.extent);
    MemoryProfiler::unregisterAllocation("RenderTarget/PostBloom", bytes);

    for (Target& target : bloom_) {
        if (target.view) vkDestroyImageView(device_.device(), target.view, nullptr);
        if (target.image) vmaDestroyImage(device_.allocator(), target.image, target.allocation);
    }
    bloom_.clear();
}

void PostProcessor::createSampler() {
    VkSamplerCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    info.magFilter = VK_FILTER_LINEAR;
    info.minFilter = VK_FILTER_LINEAR;
    info.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    info.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    if (vkCreateSampler(device_.device(), &info, nullptr, &linearSampler_) != VK_SUCCESS) {
        throw std::runtime_error("PostProcessor: failed to create sampler");
    }
}

void PostProcessor::createDescriptorResources() {
    inputLayout_ = std::make_unique<rhi::BindGroupLayout>(device_,
        std::vector<rhi::BindGroupLayoutEntry>{
            {0, rhi::BindingType::CombinedImageSampler, rhi::ShaderStages::Fragment},
        });
    updateDescriptorSets();
}

void PostProcessor::updateDescriptorSets() {
    if (!inputLayout_ || bloom_.empty() || !linearSampler_) return;

    // Bind groups are immutable: rebuild them against the current inputs. Callers
    // only re-point inputs while the GPU is idle (HDR/target recreation), the
    // same contract the old vkUpdateDescriptorSets path relied on.
    downsampleGroups_.clear();
    upsampleGroups_.clear();

    auto makeGroup = [&](VkImageView view) {
        rhi::BindGroupEntry entry;
        entry.binding = 0;
        entry.view = view;
        entry.sampler = linearSampler_;
        return std::make_unique<rhi::BindGroup>(*inputLayout_,
                                                std::vector<rhi::BindGroupEntry>{entry});
    };

    for (size_t i = 0; i < bloom_.size(); ++i) {
        downsampleGroups_.push_back(makeGroup(i == 0 ? hdrInputView_ : bloom_[i - 1].view));
    }
    for (size_t i = 0; i < bloom_.size(); ++i) {
        upsampleGroups_.push_back(makeGroup(bloom_[std::min(i + 1, bloom_.size() - 1)].view));
    }
}

void PostProcessor::createPipelines() {
    Pipeline::Desc desc;
    desc.vertPath = shaderPath("tonemap.vert.spv");
    desc.colorFormats = {rhi::vulkan::fromVk(hdrFormat_)};
    desc.bindGroupLayouts = {inputLayout_.get()};
    desc.vertexInput = false;
    desc.depthTest = false;
    desc.depthWrite = false;
    desc.cullMode = rhi::CullMode::None;
    desc.pushConstantSize = sizeof(BloomPush);

    desc.fragPath = shaderPath("bloom_downsample.frag.spv");
    desc.blendMode = rhi::BlendMode::None;
    bloomDownsamplePipeline_ = std::make_unique<Pipeline>(device_, desc);

    desc.fragPath = shaderPath("bloom_upsample.frag.spv");
    desc.blendMode = rhi::BlendMode::Additive;
    bloomUpsamplePipeline_ = std::make_unique<Pipeline>(device_, desc);
}

void PostProcessor::transitionTarget(VkCommandBuffer cmd, Target& target, VkImageLayout newLayout,
                                     VkPipelineStageFlags2 srcStage, VkAccessFlags2 srcAccess,
                                     VkPipelineStageFlags2 dstStage, VkAccessFlags2 dstAccess) {
    if (target.layout == newLayout) return;
    if (target.layout == VK_IMAGE_LAYOUT_UNDEFINED) {
        srcStage = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT;
        srcAccess = 0;
    }
    VkImageMemoryBarrier2 barrier = imageBarrier2(target.image, target.layout, newLayout,
        srcStage, srcAccess, dstStage, dstAccess);
    cmdImageBarrier(cmd, barrier);
    target.layout = newLayout;
}

void PostProcessor::beginFullscreenPass(VkCommandBuffer cmd, Target& target, bool clear) {
    VkRenderingAttachmentInfo color{};
    color.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    color.imageView = target.view;
    color.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    color.loadOp = clear ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD;
    color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    color.clearValue.color = {{0.0f, 0.0f, 0.0f, 1.0f}};

    VkRenderingInfo rendering{};
    rendering.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    rendering.renderArea.offset = {0, 0};
    rendering.renderArea.extent = target.extent;
    rendering.layerCount = 1;
    rendering.colorAttachmentCount = 1;
    rendering.pColorAttachments = &color;
    vkCmdBeginRendering(cmd, &rendering);

    VkViewport viewport{};
    viewport.width = static_cast<float>(target.extent.width);
    viewport.height = static_cast<float>(target.extent.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(cmd, 0, 1, &viewport);
    VkRect2D scissor{};
    scissor.extent = target.extent;
    vkCmdSetScissor(cmd, 0, 1, &scissor);
}

void PostProcessor::recordBloom(VkCommandBuffer cmd, const SceneSettings& settings,
                                const glm::vec4& sourceRect, GpuProfiler* profiler) {
    if (bloom_.empty()) return;

    const bool enabled = settings.bloomEnabled && settings.bloomIntensity > 0.0f && settings.bloomRadius > 0.0f;

    {
        SAIDA_GPU_PROFILE_SCOPE(profiler, cmd, "Post/BloomDownsample");
        bloomDownsamplePipeline_->bind(cmd);
        for (size_t i = 0; i < bloom_.size(); ++i) {
            Target& target = bloom_[i];
            transitionTarget(cmd, target, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_READ_BIT,
                VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);
            beginFullscreenPass(cmd, target, true);
            VkDescriptorSet downsampleSet = downsampleGroups_[i]->handle();
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                bloomDownsamplePipeline_->layout(), 0, 1, &downsampleSet, 0, nullptr);
            BloomPush push{};
            push.sourceRect = (i == 0) ? sourceRect : glm::vec4(0.0f, 0.0f, 1.0f, 1.0f);
            push.params = glm::vec4(std::max(settings.bloomThreshold, 0.0f),
                                    (enabled && i == 0) ? 1.0f : 0.0f,
                                    std::max(settings.bloomRadius, 0.0f),
                                    enabled ? 1.0f : 0.0f);
            vkCmdPushConstants(cmd, bloomDownsamplePipeline_->layout(), VK_SHADER_STAGE_FRAGMENT_BIT,
                               0, sizeof(push), &push);
            vkCmdDraw(cmd, 3, 1, 0, 0);
            vkCmdEndRendering(cmd);
            transitionTarget(cmd, target, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_READ_BIT);
        }
    }

    if (!enabled || bloom_.size() < 2) return;

    {
        SAIDA_GPU_PROFILE_SCOPE(profiler, cmd, "Post/BloomUpsample");
        bloomUpsamplePipeline_->bind(cmd);
        for (size_t reverse = bloom_.size() - 1; reverse > 0; --reverse) {
            size_t targetIndex = reverse - 1;
            Target& target = bloom_[targetIndex];
            transitionTarget(cmd, target, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_READ_BIT,
                VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);
            beginFullscreenPass(cmd, target, false);
            VkDescriptorSet upsampleSet = upsampleGroups_[targetIndex]->handle();
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                bloomUpsamplePipeline_->layout(), 0, 1, &upsampleSet, 0, nullptr);
            BloomPush push{};
            push.sourceRect = glm::vec4(0.0f, 0.0f, 1.0f, 1.0f);
            push.params = glm::vec4(0.0f, 0.0f, std::max(settings.bloomRadius, 0.0f), 0.0f);
            vkCmdPushConstants(cmd, bloomUpsamplePipeline_->layout(), VK_SHADER_STAGE_FRAGMENT_BIT,
                               0, sizeof(push), &push);
            vkCmdDraw(cmd, 3, 1, 0, 0);
            vkCmdEndRendering(cmd);
            transitionTarget(cmd, target, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_READ_BIT);
        }
    }
}

} // namespace saida
