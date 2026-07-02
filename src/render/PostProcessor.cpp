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

void PostProcessor::transitionTarget(rhi::CommandEncoder& encoder, Target& target,
                                     rhi::ResourceState to) {
    if (target.state == to) return;
    encoder.transition(target.image, target.state, to);
    target.state = to;
}

rhi::RenderPassEncoder PostProcessor::beginFullscreenPass(rhi::CommandEncoder& encoder,
                                                          Target& target, bool clear) {
    rhi::RenderPassDesc pass;
    pass.colorCount = 1;
    pass.colors[0].view = target.view;
    pass.colors[0].loadOp = clear ? rhi::LoadOp::Clear : rhi::LoadOp::Load;
    pass.width = target.extent.width;
    pass.height = target.extent.height;
    return encoder.beginRenderPass(pass);
}

void PostProcessor::recordBloom(rhi::CommandEncoder& encoder, const SceneSettings& settings,
                                const glm::vec4& sourceRect, GpuProfiler* profiler) {
    if (bloom_.empty()) return;

    const bool enabled = settings.bloomEnabled && settings.bloomIntensity > 0.0f && settings.bloomRadius > 0.0f;

    {
        SAIDA_GPU_PROFILE_SCOPE(profiler, encoder.handle(), "Post/BloomDownsample");
        for (size_t i = 0; i < bloom_.size(); ++i) {
            Target& target = bloom_[i];
            transitionTarget(encoder, target, rhi::ResourceState::ColorAttachment);
            rhi::RenderPassEncoder rp = beginFullscreenPass(encoder, target, true);
            rp.setPipeline(*bloomDownsamplePipeline_);
            rp.setBindGroup(0, *downsampleGroups_[i]);
            BloomPush push{};
            push.sourceRect = (i == 0) ? sourceRect : glm::vec4(0.0f, 0.0f, 1.0f, 1.0f);
            push.params = glm::vec4(std::max(settings.bloomThreshold, 0.0f),
                                    (enabled && i == 0) ? 1.0f : 0.0f,
                                    std::max(settings.bloomRadius, 0.0f),
                                    enabled ? 1.0f : 0.0f);
            rp.setPushConstants(&push, sizeof(push));
            rp.draw(3);
            rp.end();
            transitionTarget(encoder, target, rhi::ResourceState::ShaderRead);
        }
    }

    if (!enabled || bloom_.size() < 2) return;

    {
        SAIDA_GPU_PROFILE_SCOPE(profiler, encoder.handle(), "Post/BloomUpsample");
        for (size_t reverse = bloom_.size() - 1; reverse > 0; --reverse) {
            size_t targetIndex = reverse - 1;
            Target& target = bloom_[targetIndex];
            transitionTarget(encoder, target, rhi::ResourceState::ColorAttachment);
            rhi::RenderPassEncoder rp = beginFullscreenPass(encoder, target, false);
            rp.setPipeline(*bloomUpsamplePipeline_);
            rp.setBindGroup(0, *upsampleGroups_[targetIndex]);
            BloomPush push{};
            push.sourceRect = glm::vec4(0.0f, 0.0f, 1.0f, 1.0f);
            push.params = glm::vec4(0.0f, 0.0f, std::max(settings.bloomRadius, 0.0f), 0.0f);
            rp.setPushConstants(&push, sizeof(push));
            rp.draw(3);
            rp.end();
            transitionTarget(encoder, target, rhi::ResourceState::ShaderRead);
        }
    }
}

} // namespace saida
