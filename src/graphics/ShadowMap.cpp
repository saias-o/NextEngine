#include "graphics/ShadowMap.hpp"

#include "core/Paths.hpp"
#include "graphics/GpuSync.hpp"
#include "graphics/Mesh.hpp"
#include "graphics/VulkanDevice.hpp"

#include "vk_mem_alloc.h"

#include <glm/glm.hpp>

#include <fstream>
#include <stdexcept>
#include <vector>

namespace saida {

namespace {

std::vector<char> readFile(const std::string& filename) {
    std::ifstream file(filename, std::ios::ate | std::ios::binary);
    if (!file.is_open())
        throw std::runtime_error("failed to open file: " + filename);
    size_t fileSize = static_cast<size_t>(file.tellg());
    std::vector<char> buffer(fileSize);
    file.seekg(0);
    file.read(buffer.data(), fileSize);
    return buffer;
}

VkShaderModule createShaderModule(VkDevice device, const std::vector<char>& code) {
    VkShaderModuleCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    ci.codeSize = code.size();
    ci.pCode = reinterpret_cast<const uint32_t*>(code.data());
    VkShaderModule mod;
    if (vkCreateShaderModule(device, &ci, nullptr, &mod) != VK_SUCCESS)
        throw std::runtime_error("failed to create shadow shader module");
    return mod;
}

} // namespace

ShadowMap::ShadowMap(VulkanDevice& device, uint32_t initialResolution) : device_(device), resolution_(initialResolution) {
    format_ = device_.findSupportedFormat(
        {VK_FORMAT_D32_SFLOAT, VK_FORMAT_D16_UNORM},
        VK_IMAGE_TILING_OPTIMAL,
        VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT);

    createImage();
    createViews();
    createSampler();
    createPipeline();
}

ShadowMap::~ShadowMap() {
    vkDestroyPipeline(device_.device(), pipeline_, nullptr);
    vkDestroyPipelineLayout(device_.device(), pipelineLayout_, nullptr);
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
    info.format = format_;
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
    // layers cycle UNDEFINED -> attachment -> read-only each frame via the pass.
    VkCommandBuffer cmd = device_.beginSingleTimeCommands();
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image_;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.layerCount = kMaxShadows;
    barrier.srcAccessMask = 0;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
    device_.endSingleTimeCommands(cmd);
}

void ShadowMap::createViews() {
    VkImageViewCreateInfo arrayInfo{};
    arrayInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    arrayInfo.image = image_;
    arrayInfo.viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
    arrayInfo.format = format_;
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
        info.format = format_;
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
    auto vertCode = readFile(shaderPath("shadow.vert.spv"));
    VkShaderModule vertModule = createShaderModule(device_.device(), vertCode);

    VkPipelineShaderStageCreateInfo vertStage{};
    vertStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vertStage.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vertStage.module = vertModule;
    vertStage.pName = "main";

    auto bindingDesc = Vertex::bindingDescription();
    auto attrDescs = Vertex::attributeDescriptions();
    VkPipelineVertexInputStateCreateInfo vertexInput{};
    vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInput.vertexBindingDescriptionCount = 1;
    vertexInput.pVertexBindingDescriptions = &bindingDesc;
    vertexInput.vertexAttributeDescriptionCount = static_cast<uint32_t>(attrDescs.size());
    vertexInput.pVertexAttributeDescriptions = attrDescs.data();

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.depthBiasEnable = VK_TRUE;            // combat shadow acne
    rasterizer.depthBiasConstantFactor = 1.25f;
    rasterizer.depthBiasSlopeFactor = 1.75f;

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_TRUE;
    depthStencil.depthWriteEnable = VK_TRUE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;

    VkPipelineColorBlendStateCreateInfo colorBlend{};
    colorBlend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlend.attachmentCount = 0;  // depth-only

    std::array<VkDynamicState, 2> dynamicStates = {
        VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
    dynamicState.pDynamicStates = dynamicStates.data();

    // Push constant: mat4 mvp = lightViewProj * model.
    VkPushConstantRange pushRange{};
    pushRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    pushRange.offset = 0;
    pushRange.size = sizeof(glm::mat4);

    VkPipelineLayoutCreateInfo layoutCI{};
    layoutCI.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutCI.pushConstantRangeCount = 1;
    layoutCI.pPushConstantRanges = &pushRange;
    if (vkCreatePipelineLayout(device_.device(), &layoutCI, nullptr, &pipelineLayout_) != VK_SUCCESS)
        throw std::runtime_error("failed to create shadow pipeline layout");

    VkGraphicsPipelineCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    ci.stageCount = 1;
    ci.pStages = &vertStage;
    ci.pVertexInputState = &vertexInput;
    ci.pInputAssemblyState = &inputAssembly;
    ci.pViewportState = &viewportState;
    ci.pRasterizationState = &rasterizer;
    ci.pMultisampleState = &multisampling;
    ci.pDepthStencilState = &depthStencil;
    ci.pColorBlendState = &colorBlend;
    ci.pDynamicState = &dynamicState;
    ci.layout = pipelineLayout_;
    ci.renderPass = VK_NULL_HANDLE;
    ci.subpass = 0;

    // Dynamic rendering: depth-only, no color attachments.
    VkPipelineRenderingCreateInfo renderingInfo{};
    renderingInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    renderingInfo.depthAttachmentFormat = format_;
    ci.pNext = &renderingInfo;

    if (vkCreateGraphicsPipelines(device_.device(), device_.pipelineCache(), 1, &ci, nullptr,
        &pipeline_) != VK_SUCCESS)
        throw std::runtime_error("failed to create shadow pipeline");

    vkDestroyShaderModule(device_.device(), vertModule, nullptr);
}

void ShadowMap::record(VkCommandBuffer cmd, int count, const DrawGeometryFn& drawGeometry) {
    if (count <= 0) return;
    if (count > static_cast<int>(kMaxShadows)) count = kMaxShadows;

    VkViewport viewport{};
    viewport.width = static_cast<float>(resolution_);
    viewport.height = static_cast<float>(resolution_);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = {resolution_, resolution_};

    for (int i = 0; i < count; ++i) {
        // This layer is in SHADER_READ_ONLY (init / previous frame); move it to a
        // depth attachment. Contents are cleared so we discard with UNDEFINED.
        VkImageMemoryBarrier2 toAttach = imageBarrier2(image_,
            VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
            VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_READ_BIT,
            VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT, VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
            VK_IMAGE_ASPECT_DEPTH_BIT, static_cast<uint32_t>(i), 1);
        cmdImageBarrier(cmd, toAttach);

        VkRenderingAttachmentInfo depthAttach{};
        depthAttach.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        depthAttach.imageView = layerViews_[i];
        depthAttach.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
        depthAttach.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        depthAttach.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        depthAttach.clearValue.depthStencil = {1.0f, 0};

        VkRenderingInfo rp{};
        rp.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
        rp.renderArea.extent = {resolution_, resolution_};
        rp.layerCount = 1;
        rp.pDepthAttachment = &depthAttach;

        vkCmdBeginRendering(cmd, &rp);
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_);
        vkCmdSetViewport(cmd, 0, 1, &viewport);
        vkCmdSetScissor(cmd, 0, 1, &scissor);
        drawGeometry(cmd, pipelineLayout_, i);
        vkCmdEndRendering(cmd);

        // Back to a sampled layout for the main pass / bake.
        VkImageMemoryBarrier2 toRead = imageBarrier2(image_,
            VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL,
            VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT, VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
            VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_READ_BIT,
            VK_IMAGE_ASPECT_DEPTH_BIT, static_cast<uint32_t>(i), 1);
        cmdImageBarrier(cmd, toRead);
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
