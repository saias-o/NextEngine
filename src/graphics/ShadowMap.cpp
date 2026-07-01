#include "graphics/ShadowMap.hpp"

#include "core/Paths.hpp"
#include "graphics/Mesh.hpp"
#include "graphics/VulkanDevice.hpp"
#include "rhi/vulkan/Format.hpp"

#include "vk_mem_alloc.h"

#include <glm/glm.hpp>

#include <stdexcept>

namespace saida {

ShadowMap::ShadowMap(VulkanDevice& device, uint32_t initialResolution) : device_(device), resolution_(initialResolution) {
    VkFormat vkFormat = device_.findSupportedFormat(
        {VK_FORMAT_D32_SFLOAT, VK_FORMAT_D16_UNORM},
        VK_IMAGE_TILING_OPTIMAL,
        VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT);
    format_ = vkFormat == VK_FORMAT_D32_SFLOAT ? rhi::Format::Depth32Float : rhi::Format::Depth16;

    createImage();
    createViews();
    createSampler();
    createPipeline();
}

ShadowMap::~ShadowMap() {
    pipeline_.reset();
    vkDestroySampler(device_.device(), sampler_, nullptr);
    for (auto view : layerViews_)
        if (view) vkDestroyImageView(device_.device(), view, nullptr);
    vkDestroyImageView(device_.device(), arrayView_, nullptr);
    vmaDestroyImage(device_.allocator(), image_, allocation_);
}

void ShadowMap::createImage() {
    VkImageCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    info.imageType = VK_IMAGE_TYPE_2D;
    info.extent = {resolution_, resolution_, 1};
    info.mipLevels = 1;
    info.arrayLayers = kMaxShadows;
    info.format = rhi::vulkan::toVk(format_);
    info.tiling = VK_IMAGE_TILING_OPTIMAL;
    info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    info.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    info.samples = VK_SAMPLE_COUNT_1_BIT;

    VmaAllocationCreateInfo alloc{};
    alloc.usage = VMA_MEMORY_USAGE_AUTO;
    if (vmaCreateImage(device_.allocator(), &info, &alloc, &image_, &allocation_, nullptr) != VK_SUCCESS)
        throw std::runtime_error("failed to create shadow map image");

    // Transition every layer to the sampled (read-only) layout up front, so even
    // layers never rendered into hold the layout the descriptor expects. Rendered
    // layers cycle attachment -> read-only each frame via record().
    rhi::CommandEncoder encoder(device_.beginSingleTimeCommands());
    encoder.transition(image_, rhi::ResourceState::Undefined, rhi::ResourceState::DepthRead,
                       0, kMaxShadows);
    device_.endSingleTimeCommands(encoder.handle());
}

void ShadowMap::createViews() {
    VkImageViewCreateInfo arrayInfo{};
    arrayInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    arrayInfo.image = image_;
    arrayInfo.viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
    arrayInfo.format = rhi::vulkan::toVk(format_);
    arrayInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    arrayInfo.subresourceRange.levelCount = 1;
    arrayInfo.subresourceRange.layerCount = kMaxShadows;
    if (vkCreateImageView(device_.device(), &arrayInfo, nullptr, &arrayView_) != VK_SUCCESS)
        throw std::runtime_error("failed to create shadow array view");

    for (uint32_t i = 0; i < kMaxShadows; ++i) {
        VkImageViewCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        info.image = image_;
        info.viewType = VK_IMAGE_VIEW_TYPE_2D;
        info.format = rhi::vulkan::toVk(format_);
        info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
        info.subresourceRange.levelCount = 1;
        info.subresourceRange.baseArrayLayer = i;
        info.subresourceRange.layerCount = 1;
        if (vkCreateImageView(device_.device(), &info, nullptr, &layerViews_[i]) != VK_SUCCESS)
            throw std::runtime_error("failed to create shadow layer view");
    }
}

void ShadowMap::createSampler() {
    VkSamplerCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    ci.magFilter = VK_FILTER_LINEAR;
    ci.minFilter = VK_FILTER_LINEAR;
    ci.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    ci.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    ci.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    ci.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    ci.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;  // outside frustum = lit
    ci.unnormalizedCoordinates = VK_FALSE;
    ci.compareEnable = VK_TRUE;                            // hardware PCF
    ci.compareOp = VK_COMPARE_OP_LESS;
    if (vkCreateSampler(device_.device(), &ci, nullptr, &sampler_) != VK_SUCCESS)
        throw std::runtime_error("failed to create shadow sampler");
}

void ShadowMap::createPipeline() {
    // Depth-only: a single vertex stage, no fragment shader, no color attachment.
    // Push constant: mat4 mvp = lightViewProj * model (vertex stage only).
    rhi::Pipeline::Desc desc;
    desc.vertPath = shaderPath("shadow.vert.spv");
    desc.depthFormat = format_;
    desc.pushConstantSize = sizeof(glm::mat4);
    desc.pushConstantStages = rhi::ShaderStages::Vertex;
    desc.depthBias = true;  // combat shadow acne
    desc.depthBiasConstant = 1.25f;
    desc.depthBiasSlope = 1.75f;
    pipeline_ = std::make_unique<rhi::Pipeline>(device_, desc);
}

void ShadowMap::record(rhi::CommandEncoder& encoder, int count, const DrawGeometryFn& drawGeometry) {
    if (count <= 0) return;
    if (count > static_cast<int>(kMaxShadows)) count = kMaxShadows;

    for (int i = 0; i < count; ++i) {
        // This layer is sampled (init / previous frame); move it to a depth
        // attachment. Contents are cleared by the pass, so discard them.
        encoder.transition(image_, rhi::ResourceState::DepthRead, rhi::ResourceState::DepthWrite,
                           static_cast<uint32_t>(i), 1, /*discardContents=*/true);

        rhi::RenderPassDesc pass;
        pass.width = resolution_;
        pass.height = resolution_;
        pass.depth.view = layerViews_[i];
        pass.depth.loadOp = rhi::LoadOp::Clear;
        pass.depth.clearDepth = 1.0f;

        rhi::RenderPassEncoder rp = encoder.beginRenderPass(pass);
        rp.setPipeline(*pipeline_);
        drawGeometry(rp, i);
        rp.end();

        // Back to a sampled layout for the main pass / bake.
        encoder.transition(image_, rhi::ResourceState::DepthWrite, rhi::ResourceState::DepthRead,
                           static_cast<uint32_t>(i), 1);
    }
}

bool ShadowMap::resize(uint32_t newResolution) {
    if (resolution_ == newResolution) return false;

    resolution_ = newResolution;

    // Wait for device to finish before destroying resources
    vkDeviceWaitIdle(device_.device());

    // Destroy old views and image
    for (auto view : layerViews_) {
        if (view) vkDestroyImageView(device_.device(), view, nullptr);
    }
    layerViews_.fill(VK_NULL_HANDLE);

    if (arrayView_) {
        vkDestroyImageView(device_.device(), arrayView_, nullptr);
        arrayView_ = VK_NULL_HANDLE;
    }

    if (image_) {
        vmaDestroyImage(device_.allocator(), image_, allocation_);
        image_ = VK_NULL_HANDLE;
        allocation_ = VK_NULL_HANDLE;
    }

    // Recreate image and views (sampler and pipeline remain valid as they don't depend on resolution)
    createImage();
    createViews();

    return true;
}

} // namespace saida
