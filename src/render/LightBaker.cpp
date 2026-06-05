#include "render/LightBaker.hpp"

#include "core/Paths.hpp"
#include "graphics/GpuSync.hpp"
#include "graphics/Mesh.hpp"
#include "graphics/VulkanDevice.hpp"
#include "scene/MeshNode.hpp"
#include "scene/Scene.hpp"

#include "vk_mem_alloc.h"

#include <glm/glm.hpp>

#include <array>
#include <fstream>
#include <stdexcept>
#include <vector>

namespace ne {

namespace {

constexpr uint32_t kMaxBakedInstances = 128;

std::vector<char> readFile(const std::string& filename) {
    std::ifstream file(filename, std::ios::ate | std::ios::binary);
    if (!file.is_open())
        throw std::runtime_error("failed to open file: " + filename);
    size_t size = static_cast<size_t>(file.tellg());
    std::vector<char> buffer(size);
    file.seekg(0);
    file.read(buffer.data(), size);
    return buffer;
}

VkShaderModule createShaderModule(VkDevice device, const std::vector<char>& code) {
    VkShaderModuleCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    ci.codeSize = code.size();
    ci.pCode = reinterpret_cast<const uint32_t*>(code.data());
    VkShaderModule mod;
    if (vkCreateShaderModule(device, &ci, nullptr, &mod) != VK_SUCCESS)
        throw std::runtime_error("failed to create bake shader module");
    return mod;
}

} // namespace

LightBaker::LightBaker(VulkanDevice& device, VkDescriptorSetLayout globalSetLayout)
    : device_(device) {
    format_ = device_.findSupportedFormat(
        {VK_FORMAT_R16G16B16A16_SFLOAT, VK_FORMAT_R8G8B8A8_UNORM},
        VK_IMAGE_TILING_OPTIMAL,
        VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT);

    createSetLayoutAndPool();
    createSampler();
    createPipeline(globalSetLayout);
    createDefaultLightmap();
}

LightBaker::~LightBaker() {
    for (auto& [node, lm] : lightmaps_)
        destroyLightmap(lm);
    vkDestroyImageView(device_.device(), defaultView_, nullptr);
    vmaDestroyImage(device_.allocator(), defaultImage_, defaultAllocation_);
    vkDestroySampler(device_.device(), sampler_, nullptr);
    vkDestroyPipeline(device_.device(), pipeline_, nullptr);
    vkDestroyPipelineLayout(device_.device(), pipelineLayout_, nullptr);
    vkDestroyDescriptorPool(device_.device(), pool_, nullptr);
    vkDestroyDescriptorSetLayout(device_.device(), setLayout_, nullptr);
}

void LightBaker::createSetLayoutAndPool() {
    VkDescriptorSetLayoutBinding binding{};
    binding.binding = 0;
    binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    binding.descriptorCount = 1;
    binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    ci.bindingCount = 1;
    ci.pBindings = &binding;
    if (vkCreateDescriptorSetLayout(device_.device(), &ci, nullptr, &setLayout_) != VK_SUCCESS)
        throw std::runtime_error("failed to create lightmap set layout");

    VkDescriptorPoolSize poolSize{};
    poolSize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSize.descriptorCount = kMaxBakedInstances + 1;  // + default

    VkDescriptorPoolCreateInfo pci{};
    pci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pci.poolSizeCount = 1;
    pci.pPoolSizes = &poolSize;
    pci.maxSets = kMaxBakedInstances + 1;
    if (vkCreateDescriptorPool(device_.device(), &pci, nullptr, &pool_) != VK_SUCCESS)
        throw std::runtime_error("failed to create lightmap descriptor pool");
}

void LightBaker::createSampler() {
    VkSamplerCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    ci.magFilter = VK_FILTER_LINEAR;
    ci.minFilter = VK_FILTER_LINEAR;
    ci.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    ci.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    ci.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    ci.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    ci.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK;
    if (vkCreateSampler(device_.device(), &ci, nullptr, &sampler_) != VK_SUCCESS)
        throw std::runtime_error("failed to create lightmap sampler");
}

void LightBaker::createPipeline(VkDescriptorSetLayout globalSetLayout) {
    auto vertCode = readFile(shaderPath("bake.vert.spv"));
    auto fragCode = readFile(shaderPath("bake.frag.spv"));
    VkShaderModule vert = createShaderModule(device_.device(), vertCode);
    VkShaderModule frag = createShaderModule(device_.device(), fragCode);

    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vert;
    stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = frag;
    stages[1].pName = "main";

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
    rasterizer.cullMode = VK_CULL_MODE_NONE;  // fill every lightmap texel
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineColorBlendAttachmentState blendAttach{};
    blendAttach.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT
                               | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    blendAttach.blendEnable = VK_FALSE;

    VkPipelineColorBlendStateCreateInfo colorBlend{};
    colorBlend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlend.attachmentCount = 1;
    colorBlend.pAttachments = &blendAttach;

    std::array<VkDynamicState, 2> dynamicStates = {
        VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
    dynamicState.pDynamicStates = dynamicStates.data();

    // Push constant: model matrix (bake.vert needs only the model).
    VkPushConstantRange pushRange{};
    pushRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    pushRange.offset = 0;
    pushRange.size = sizeof(glm::mat4);

    VkPipelineLayoutCreateInfo layoutCI{};
    layoutCI.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutCI.setLayoutCount = 1;
    layoutCI.pSetLayouts = &globalSetLayout;  // set 0: lighting + shadow maps
    layoutCI.pushConstantRangeCount = 1;
    layoutCI.pPushConstantRanges = &pushRange;
    if (vkCreatePipelineLayout(device_.device(), &layoutCI, nullptr, &pipelineLayout_) != VK_SUCCESS)
        throw std::runtime_error("failed to create bake pipeline layout");

    VkGraphicsPipelineCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    ci.stageCount = 2;
    ci.pStages = stages;
    ci.pVertexInputState = &vertexInput;
    ci.pInputAssemblyState = &inputAssembly;
    ci.pViewportState = &viewportState;
    ci.pRasterizationState = &rasterizer;
    ci.pMultisampleState = &multisampling;
    ci.pColorBlendState = &colorBlend;
    ci.pDynamicState = &dynamicState;
    ci.layout = pipelineLayout_;
    ci.renderPass = VK_NULL_HANDLE;
    ci.subpass = 0;

    // Dynamic rendering: a single color attachment (the lightmap).
    VkPipelineRenderingCreateInfo renderingInfo{};
    renderingInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    renderingInfo.colorAttachmentCount = 1;
    renderingInfo.pColorAttachmentFormats = &format_;
    ci.pNext = &renderingInfo;

    if (vkCreateGraphicsPipelines(device_.device(), device_.pipelineCache(), 1, &ci, nullptr,
        &pipeline_) != VK_SUCCESS)
        throw std::runtime_error("failed to create bake pipeline");

    vkDestroyShaderModule(device_.device(), frag, nullptr);
    vkDestroyShaderModule(device_.device(), vert, nullptr);
}

VkDescriptorSet LightBaker::allocateSet(VkImageView view) {
    VkDescriptorSetAllocateInfo alloc{};
    alloc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    alloc.descriptorPool = pool_;
    alloc.descriptorSetCount = 1;
    alloc.pSetLayouts = &setLayout_;
    VkDescriptorSet set;
    if (vkAllocateDescriptorSets(device_.device(), &alloc, &set) != VK_SUCCESS)
        throw std::runtime_error("failed to allocate lightmap descriptor set");

    VkDescriptorImageInfo info{};
    info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    info.imageView = view;
    info.sampler = sampler_;

    VkWriteDescriptorSet write{};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = set;
    write.dstBinding = 0;
    write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.descriptorCount = 1;
    write.pImageInfo = &info;
    vkUpdateDescriptorSets(device_.device(), 1, &write, 0, nullptr);
    return set;
}

LightBaker::Lightmap LightBaker::createLightmap() {
    Lightmap lm;

    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent = {kLightmapSize, kLightmapSize, 1};
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.format = format_;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo alloc{};
    alloc.usage = VMA_MEMORY_USAGE_AUTO;
    if (vmaCreateImage(device_.allocator(), &imageInfo, &alloc, &lm.image, &lm.allocation, nullptr) != VK_SUCCESS)
        throw std::runtime_error("failed to create lightmap image");

    lm.view = device_.createImageView(lm.image, format_, VK_IMAGE_ASPECT_COLOR_BIT);
    lm.set = allocateSet(lm.view);
    return lm;
}

void LightBaker::destroyLightmap(Lightmap& lm) {
    vkDestroyImageView(device_.device(), lm.view, nullptr);
    vmaDestroyImage(device_.allocator(), lm.image, lm.allocation);
    // lm.set is freed when the pool is destroyed.
}

void LightBaker::createDefaultLightmap() {
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent = {1, 1, 1};
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.format = format_;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo alloc{};
    alloc.usage = VMA_MEMORY_USAGE_AUTO;
    if (vmaCreateImage(device_.allocator(), &imageInfo, &alloc, &defaultImage_, &defaultAllocation_, nullptr) != VK_SUCCESS)
        throw std::runtime_error("failed to create default lightmap image");

    // Clear to white and leave it in the shader-read layout. It is bound for
    // non-baked draws but never actually sampled (the shader branches it out).
    VkCommandBuffer cmd = device_.beginSingleTimeCommands();
    VkImageMemoryBarrier toDst{};
    toDst.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    toDst.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    toDst.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    toDst.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toDst.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toDst.image = defaultImage_;
    toDst.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    toDst.srcAccessMask = 0;
    toDst.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
        0, 0, nullptr, 0, nullptr, 1, &toDst);

    VkClearColorValue white{};
    white.float32[0] = white.float32[1] = white.float32[2] = white.float32[3] = 1.0f;
    VkImageSubresourceRange range{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    vkCmdClearColorImage(cmd, defaultImage_, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &white, 1, &range);

    VkImageMemoryBarrier toRead = toDst;
    toRead.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    toRead.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    toRead.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    toRead.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        0, 0, nullptr, 0, nullptr, 1, &toRead);
    device_.endSingleTimeCommands(cmd);

    defaultView_ = device_.createImageView(defaultImage_, format_, VK_IMAGE_ASPECT_COLOR_BIT);
    defaultSet_ = allocateSet(defaultView_);
}

void LightBaker::prepare(Scene& scene) {
    for (MeshNode* node : scene.meshes()) {
        if (node->includeInLightBaking() && node->mesh() && !lightmaps_.count(node))
            lightmaps_.emplace(node, createLightmap());
    }
}

void LightBaker::record(VkCommandBuffer cmd, VkDescriptorSet globalSet, Scene& scene) {
    VkViewport viewport{};
    viewport.width = static_cast<float>(kLightmapSize);
    viewport.height = static_cast<float>(kLightmapSize);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    VkRect2D scissor{};
    scissor.extent = {kLightmapSize, kLightmapSize};

    for (MeshNode* node : scene.meshes()) {
        if (!node->includeInLightBaking()) continue;
        Mesh* mesh = node->mesh();
        if (!mesh) continue;
        auto it = lightmaps_.find(node);
        if (it == lightmaps_.end()) continue;
        const Lightmap& lm = it->second;

        // Lightmap → color attachment (discard previous contents, we clear).
        VkImageMemoryBarrier2 toAttach = imageBarrier2(lm.image,
            VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_READ_BIT,
            VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);
        cmdImageBarrier(cmd, toAttach);

        VkRenderingAttachmentInfo colorAttach{};
        colorAttach.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        colorAttach.imageView = lm.view;
        colorAttach.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        colorAttach.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        colorAttach.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        colorAttach.clearValue.color = {{0.0f, 0.0f, 0.0f, 1.0f}};

        VkRenderingInfo rp{};
        rp.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
        rp.renderArea.extent = {kLightmapSize, kLightmapSize};
        rp.layerCount = 1;
        rp.colorAttachmentCount = 1;
        rp.pColorAttachments = &colorAttach;

        vkCmdBeginRendering(cmd, &rp);
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout_,
            0, 1, &globalSet, 0, nullptr);
        vkCmdSetViewport(cmd, 0, 1, &viewport);
        vkCmdSetScissor(cmd, 0, 1, &scissor);
        glm::mat4 model = node->worldTransform();
        vkCmdPushConstants(cmd, pipelineLayout_, VK_SHADER_STAGE_VERTEX_BIT,
            0, sizeof(glm::mat4), &model);
        mesh->bind(cmd);
        mesh->draw(cmd);
        vkCmdEndRendering(cmd);

        // → sampled layout for the main pass.
        VkImageMemoryBarrier2 toRead = imageBarrier2(lm.image,
            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
            VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_READ_BIT);
        cmdImageBarrier(cmd, toRead);
    }
}

VkDescriptorSet LightBaker::lightmapSet(MeshNode* node) const {
    auto it = lightmaps_.find(node);
    return it != lightmaps_.end() ? it->second.set : defaultSet_;
}

} // namespace ne
