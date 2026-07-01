#include "render/GIVolume.hpp"

#include "graphics/VulkanDevice.hpp"
#include "graphics/Buffer.hpp"
#include "graphics/Mesh.hpp"
#include "graphics/Material.hpp"
#include "graphics/GpuProfiler.hpp"
#include "graphics/GpuSync.hpp"
#include "graphics/ComputePipeline.hpp"
#include "scene/Scene.hpp"
#include "scene/MeshNode.hpp"
#include "core/Paths.hpp"
#include "core/Log.hpp"

#include "vk_mem_alloc.h"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include <array>
#include <cmath>
#include <fstream>
#include <stdexcept>
#include <vector>

namespace saida {

namespace {
constexpr VkFormat kIrradianceFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
constexpr VkFormat kVisibilityFormat = VK_FORMAT_R16G16_SFLOAT;
constexpr VkFormat kVoxelFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
// Atlases are compute-written (P2), sampled in the lighting pass, and (P0)
// cleared once — hence STORAGE | SAMPLED | TRANSFER_DST.
constexpr VkImageUsageFlags kAtlasUsage =
    VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;

// Mirror of VoxelUBO in voxelize.vert/frag (std140).
struct VoxelUBOData {
    glm::vec4 origin;
    glm::vec4 extent;
    glm::ivec4 res;
    glm::mat4 axisVP[3];
};

// Push constant for the voxelize pipeline (vertex stage).
struct VoxelPush {
    glm::mat4 model;
    uint32_t axis;
};

// Per-element of the rays buffer (mirror of Ray in ddgi_trace/blend).
struct RayData {
    glm::vec4 dirDist;
    glm::vec4 radiance;
};

// Compute push constants (mirror the shaders).
struct TracePush {
    glm::mat4 randomRot;
    int32_t raysPerProbe;
    int32_t probeCount;
};
struct BlendPush {
    int32_t mode;
    int32_t raysPerProbe;
    int32_t probeCount;
    float hysteresis;
    float distExponent;
};
struct BorderPush {
    int32_t mode;
    int32_t probeCount;
};

std::vector<char> readFile(const std::string& filename) {
    std::ifstream file(filename, std::ios::ate | std::ios::binary);
    if (!file.is_open())
        throw std::runtime_error("GIVolume: failed to open " + filename);
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
        throw std::runtime_error("GIVolume: failed to create shader module");
    return mod;
}
}

GIVolume::GIVolume(VulkanDevice& device, const GIVolumeDesc& desc,
                   VkDescriptorSetLayout materialSetLayout, VkDescriptorSetLayout globalSetLayout)
    : device_(device), desc_(desc) {
    // Lay probes out in a roughly square 2D atlas.
    probesPerRow_ = std::max(1, static_cast<int>(std::ceil(std::sqrt(
                        static_cast<double>(desc_.probeCount())))));

    glm::ivec2 irr = irradianceAtlasSize();
    glm::ivec2 vis = visibilityAtlasSize();
    for (int i = 0; i < 2; ++i) {
        irradiance_[i] = std::make_unique<StorageImage>(
            device_, irr.x, irr.y, kIrradianceFormat, kAtlasUsage);
        visibility_[i] = std::make_unique<StorageImage>(
            device_, vis.x, vis.y, kVisibilityFormat, kAtlasUsage);
    }

    createSampler();
    fillConstant();
    createVoxelResources(materialSetLayout);
    createComputeResources(globalSetLayout);

    Log::info("GIVolume: ", desc_.probeCount(), " probes (", desc_.counts.x, "x",
              desc_.counts.y, "x", desc_.counts.z, "), irradiance atlas ", irr.x,
              "x", irr.y, ", visibility atlas ", vis.x, "x", vis.y,
              ", voxel grid ", desc_.voxelResolution, "^3");
}

GIVolume::~GIVolume() {
    // Compute pipelines destroy themselves (unique_ptr<ComputePipeline>).
    if (giComputePool_) vkDestroyDescriptorPool(device_.device(), giComputePool_, nullptr);
    if (giComputeSetLayout_) vkDestroyDescriptorSetLayout(device_.device(), giComputeSetLayout_, nullptr);
    if (voxelPipeline_) vkDestroyPipeline(device_.device(), voxelPipeline_, nullptr);
    if (voxelPipelineLayout_) vkDestroyPipelineLayout(device_.device(), voxelPipelineLayout_, nullptr);
    if (voxelPool_) vkDestroyDescriptorPool(device_.device(), voxelPool_, nullptr);
    if (voxelSetLayout_) vkDestroyDescriptorSetLayout(device_.device(), voxelSetLayout_, nullptr);
    if (voxelView_) vkDestroyImageView(device_.device(), voxelView_, nullptr);
    if (voxelImage_) vmaDestroyImage(device_.allocator(), voxelImage_, voxelAllocation_);
    if (sampler_) vkDestroySampler(device_.device(), sampler_, nullptr);
    // StorageImages and the UBO/rays Buffers destroy themselves.
}

void GIVolume::createSampler() {
    VkSamplerCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    ci.magFilter = VK_FILTER_LINEAR;
    ci.minFilter = VK_FILTER_LINEAR;
    ci.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    ci.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    ci.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    ci.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    if (vkCreateSampler(device_.device(), &ci, nullptr, &sampler_) != VK_SUCCESS)
        throw std::runtime_error("GIVolume: failed to create sampler");
}

void GIVolume::fillConstant() {
    // Irradiance stores mean incident radiance (diffuse = albedo * value). A
    // neutral ambient until the first DDGI update writes real data.
    VkClearColorValue irrClear{};
    irrClear.float32[0] = 0.10f;
    irrClear.float32[1] = 0.10f;
    irrClear.float32[2] = 0.10f;
    irrClear.float32[3] = 1.0f;

    // Visibility: huge mean distance so the Chebyshev test always returns "fully
    // visible" (no occlusion) until real data is baked/updated.
    VkClearColorValue visClear{};
    visClear.float32[0] = 1.0e4f;   // mean distance
    visClear.float32[1] = 1.0e8f;   // mean distance^2
    visClear.float32[2] = 0.0f;
    visClear.float32[3] = 0.0f;

    VkImageSubresourceRange range{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

    VkCommandBuffer cmd = device_.beginSingleTimeCommands();
    auto clear = [&](StorageImage& img, const VkClearColorValue& color) {
        img.transition(cmd, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                       VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0,
                       VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT);
        vkCmdClearColorImage(cmd, img.image(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                             &color, 1, &range);
        // Atlases live in GENERAL: compute writes them as storage images and the
        // lighting pass samples them in GENERAL (avoids per-frame layout churn).
        img.transition(cmd, VK_IMAGE_LAYOUT_GENERAL,
                       VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
                       VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                       VK_ACCESS_SHADER_READ_BIT);
    };
    for (int i = 0; i < 2; ++i) {
        clear(*irradiance_[i], irrClear);
        clear(*visibility_[i], visClear);
    }
    device_.endSingleTimeCommands(cmd);
}

void GIVolume::createVoxelResources(VkDescriptorSetLayout materialSetLayout) {
    const uint32_t res = static_cast<uint32_t>(desc_.voxelResolution);

    // 3D albedo grid.
    VkImageCreateInfo img{};
    img.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    img.imageType = VK_IMAGE_TYPE_3D;
    img.extent = {res, res, res};
    img.mipLevels = 1;
    img.arrayLayers = 1;
    img.format = kVoxelFormat;
    img.tiling = VK_IMAGE_TILING_OPTIMAL;
    img.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    img.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    img.samples = VK_SAMPLE_COUNT_1_BIT;
    img.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    VmaAllocationCreateInfo alloc{};
    alloc.usage = VMA_MEMORY_USAGE_AUTO;
    if (vmaCreateImage(device_.allocator(), &img, &alloc, &voxelImage_, &voxelAllocation_, nullptr) != VK_SUCCESS)
        throw std::runtime_error("GIVolume: failed to create voxel image");

    VkImageViewCreateInfo view{};
    view.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    view.image = voxelImage_;
    view.viewType = VK_IMAGE_VIEW_TYPE_3D;
    view.format = kVoxelFormat;
    view.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    if (vkCreateImageView(device_.device(), &view, nullptr, &voxelView_) != VK_SUCCESS)
        throw std::runtime_error("GIVolume: failed to create voxel image view");

    voxelUbo_ = std::make_unique<Buffer>(device_, sizeof(VoxelUBOData),
        rhi::BufferUsage::Uniform, MemoryUsage::HostVisible);

    // Descriptor set: binding 0 = storage image, binding 1 = UBO.
    std::array<VkDescriptorSetLayoutBinding, 2> b{};
    b[0].binding = 0;
    b[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    b[0].descriptorCount = 1;
    b[0].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    b[1].binding = 1;
    b[1].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    b[1].descriptorCount = 1;
    b[1].stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo layoutCI{};
    layoutCI.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutCI.bindingCount = static_cast<uint32_t>(b.size());
    layoutCI.pBindings = b.data();
    if (vkCreateDescriptorSetLayout(device_.device(), &layoutCI, nullptr, &voxelSetLayout_) != VK_SUCCESS)
        throw std::runtime_error("GIVolume: failed to create voxel set layout");

    std::array<VkDescriptorPoolSize, 2> ps{};
    ps[0].type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    ps[0].descriptorCount = 1;
    ps[1].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    ps[1].descriptorCount = 1;
    VkDescriptorPoolCreateInfo poolCI{};
    poolCI.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolCI.poolSizeCount = static_cast<uint32_t>(ps.size());
    poolCI.pPoolSizes = ps.data();
    poolCI.maxSets = 1;
    if (vkCreateDescriptorPool(device_.device(), &poolCI, nullptr, &voxelPool_) != VK_SUCCESS)
        throw std::runtime_error("GIVolume: failed to create voxel descriptor pool");

    VkDescriptorSetAllocateInfo ai{};
    ai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    ai.descriptorPool = voxelPool_;
    ai.descriptorSetCount = 1;
    ai.pSetLayouts = &voxelSetLayout_;
    if (vkAllocateDescriptorSets(device_.device(), &ai, &voxelSet_) != VK_SUCCESS)
        throw std::runtime_error("GIVolume: failed to allocate voxel descriptor set");

    VkDescriptorImageInfo imgInfo{};
    imgInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;  // storage image is written in GENERAL
    imgInfo.imageView = voxelView_;
    VkDescriptorBufferInfo bufInfo{};
    bufInfo.buffer = voxelUbo_->handle();
    bufInfo.offset = 0;
    bufInfo.range = sizeof(VoxelUBOData);
    std::array<VkWriteDescriptorSet, 2> w{};
    w[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    w[0].dstSet = voxelSet_;
    w[0].dstBinding = 0;
    w[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    w[0].descriptorCount = 1;
    w[0].pImageInfo = &imgInfo;
    w[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    w[1].dstSet = voxelSet_;
    w[1].dstBinding = 1;
    w[1].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    w[1].descriptorCount = 1;
    w[1].pBufferInfo = &bufInfo;
    vkUpdateDescriptorSets(device_.device(), static_cast<uint32_t>(w.size()), w.data(), 0, nullptr);

    // --- Voxelize pipeline (attachment-less raster; writes only via imageStore). ---
    auto vertCode = readFile(shaderPath("voxelize.vert.spv"));
    auto fragCode = readFile(shaderPath("voxelize.frag.spv"));
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
    VkPipelineVertexInputStateCreateInfo vi{};
    vi.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vi.vertexBindingDescriptionCount = 1;
    vi.pVertexBindingDescriptions = &bindingDesc;
    vi.vertexAttributeDescriptionCount = static_cast<uint32_t>(attrDescs.size());
    vi.pVertexAttributeDescriptions = attrDescs.data();

    VkPipelineInputAssemblyStateCreateInfo ia{};
    ia.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo vp{};
    vp.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    vp.viewportCount = 1;
    vp.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rs{};
    rs.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rs.polygonMode = VK_POLYGON_MODE_FILL;
    rs.lineWidth = 1.0f;
    rs.cullMode = VK_CULL_MODE_NONE;  // rasterize every triangle from every axis

    VkPipelineMultisampleStateCreateInfo ms{};
    ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo ds{};
    ds.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    ds.depthTestEnable = VK_FALSE;
    ds.depthWriteEnable = VK_FALSE;

    // Attachment-less: no color blend attachments.
    VkPipelineColorBlendStateCreateInfo cb{};
    cb.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    cb.attachmentCount = 0;

    std::array<VkDynamicState, 2> dyn = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynCI{};
    dynCI.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynCI.dynamicStateCount = static_cast<uint32_t>(dyn.size());
    dynCI.pDynamicStates = dyn.data();

    VkPushConstantRange push{};
    push.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    push.offset = 0;
    push.size = sizeof(VoxelPush);

    std::array<VkDescriptorSetLayout, 2> setLayouts = {voxelSetLayout_, materialSetLayout};
    VkPipelineLayoutCreateInfo plCI{};
    plCI.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    plCI.setLayoutCount = static_cast<uint32_t>(setLayouts.size());
    plCI.pSetLayouts = setLayouts.data();
    plCI.pushConstantRangeCount = 1;
    plCI.pPushConstantRanges = &push;
    if (vkCreatePipelineLayout(device_.device(), &plCI, nullptr, &voxelPipelineLayout_) != VK_SUCCESS)
        throw std::runtime_error("GIVolume: failed to create voxelize pipeline layout");

    VkPipelineRenderingCreateInfo rendering{};
    rendering.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    rendering.colorAttachmentCount = 0;
    rendering.depthAttachmentFormat = VK_FORMAT_UNDEFINED;

    VkGraphicsPipelineCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    ci.pNext = &rendering;
    ci.stageCount = 2;
    ci.pStages = stages;
    ci.pVertexInputState = &vi;
    ci.pInputAssemblyState = &ia;
    ci.pViewportState = &vp;
    ci.pRasterizationState = &rs;
    ci.pMultisampleState = &ms;
    ci.pDepthStencilState = &ds;
    ci.pColorBlendState = &cb;
    ci.pDynamicState = &dynCI;
    ci.layout = voxelPipelineLayout_;
    if (vkCreateGraphicsPipelines(device_.device(), device_.pipelineCache(), 1, &ci, nullptr,
        &voxelPipeline_) != VK_SUCCESS)
        throw std::runtime_error("GIVolume: failed to create voxelize pipeline");

    vkDestroyShaderModule(device_.device(), frag, nullptr);
    vkDestroyShaderModule(device_.device(), vert, nullptr);
}

void GIVolume::voxelize(VkCommandBuffer cmd, Scene& scene, GpuProfiler* profiler) {
    SAIDA_GPU_PROFILE_SCOPE(profiler, cmd, "DDGI/Voxelize");
    const uint32_t res = static_cast<uint32_t>(desc_.voxelResolution);
    glm::vec3 extent = worldExtent();
    glm::vec3 center = desc_.origin + extent * 0.5f;

    // Three orthographic view-projections, one per dominant axis, each covering
    // the whole volume box. Orientation does not matter (the frag derives the
    // voxel from world position), only that triangles get rasterized.
    VoxelUBOData ubo{};
    ubo.origin = glm::vec4(desc_.origin, 0.0f);
    ubo.extent = glm::vec4(extent, 0.0f);
    ubo.res = glm::ivec4(res, res, res, 0);
    auto axisVP = [&](glm::vec3 dir, glm::vec3 up, float w, float h, float depth) {
        glm::mat4 v = glm::lookAt(center + dir * depth, center, up);
        glm::mat4 p = glm::ortho(-w * 0.5f, w * 0.5f, -h * 0.5f, h * 0.5f, 0.0f, 2.0f * depth);
        return p * v;
    };
    ubo.axisVP[0] = axisVP({0, 0, 1}, {0, 1, 0}, extent.x, extent.y, extent.z); // along Z
    ubo.axisVP[1] = axisVP({1, 0, 0}, {0, 1, 0}, extent.z, extent.y, extent.x); // along X
    ubo.axisVP[2] = axisVP({0, 1, 0}, {0, 0, 1}, extent.x, extent.z, extent.y); // along Y
    voxelUbo_->write(&ubo, sizeof(ubo));

    VkImageSubresourceRange range{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

    // Clear the grid (UNDEFINED -> TRANSFER_DST), then -> GENERAL for imageStore.
    VkImageMemoryBarrier2 toClear = imageBarrier2(voxelImage_,
        VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, 0,
        VK_PIPELINE_STAGE_2_CLEAR_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT);
    cmdImageBarrier(cmd, toClear);

    VkClearColorValue zero{};
    vkCmdClearColorImage(cmd, voxelImage_, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &zero, 1, &range);

    VkImageMemoryBarrier2 toGeneral = imageBarrier2(voxelImage_,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL,
        VK_PIPELINE_STAGE_2_CLEAR_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
        VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_WRITE_BIT);
    cmdImageBarrier(cmd, toGeneral);

    // Attachment-less rendering at voxel resolution.
    VkRenderingInfo rp{};
    rp.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    rp.renderArea.extent = {res, res};
    rp.layerCount = 1;
    rp.colorAttachmentCount = 0;
    vkCmdBeginRendering(cmd, &rp);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, voxelPipeline_);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, voxelPipelineLayout_,
        0, 1, &voxelSet_, 0, nullptr);

    VkViewport viewport{};
    viewport.width = static_cast<float>(res);
    viewport.height = static_cast<float>(res);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(cmd, 0, 1, &viewport);
    VkRect2D scissor{};
    scissor.extent = {res, res};
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    for (uint32_t axis = 0; axis < 3; ++axis) {
        for (MeshNode* node : scene.meshes()) {
            Mesh* mesh = node->mesh();
            Material* mat = node->material();
            if (!mesh || !mat) continue;
            VkDescriptorSet matSet = mat->descriptorSet();
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, voxelPipelineLayout_,
                1, 1, &matSet, 0, nullptr);
            VoxelPush pc{node->worldTransform(), axis};
            vkCmdPushConstants(cmd, voxelPipelineLayout_, VK_SHADER_STAGE_VERTEX_BIT,
                0, sizeof(pc), &pc);
            mesh->bind(cmd);
            mesh->draw(cmd);
        }
    }

    vkCmdEndRendering(cmd);

    // GENERAL -> sampled for the main/debug pass AND the DDGI trace (compute).
    VkImageMemoryBarrier2 toRead = imageBarrier2(voxelImage_,
        VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_WRITE_BIT,
        VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
        VK_ACCESS_2_SHADER_READ_BIT);
    cmdImageBarrier(cmd, toRead);
}

void GIVolume::createComputeResources(VkDescriptorSetLayout globalSetLayout) {
    const int probeCount = desc_.probeCount();
    raysBuffer_ = std::make_unique<Buffer>(device_,
        static_cast<VkDeviceSize>(probeCount) * desc_.raysPerProbe * sizeof(RayData),
        rhi::BufferUsage::Storage, MemoryUsage::GpuOnly);

    // Set layout: 0=rays SSBO, 1/2=write irr/vis (storage img), 3/4=prev irr/vis.
    std::array<VkDescriptorSetLayoutBinding, 5> b{};
    b[0].binding = 0; b[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    for (int i = 1; i < 5; ++i) b[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    for (int i = 0; i < 5; ++i) { b[i].binding = i; b[i].descriptorCount = 1; b[i].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT; }

    VkDescriptorSetLayoutCreateInfo lci{};
    lci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    lci.bindingCount = static_cast<uint32_t>(b.size());
    lci.pBindings = b.data();
    if (vkCreateDescriptorSetLayout(device_.device(), &lci, nullptr, &giComputeSetLayout_) != VK_SUCCESS)
        throw std::runtime_error("GIVolume: failed to create compute set layout");

    std::array<VkDescriptorPoolSize, 2> ps{};
    ps[0].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;  ps[0].descriptorCount = 2;
    ps[1].type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;   ps[1].descriptorCount = 2 * 4;
    VkDescriptorPoolCreateInfo pci{};
    pci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pci.poolSizeCount = static_cast<uint32_t>(ps.size());
    pci.pPoolSizes = ps.data();
    pci.maxSets = 2;
    if (vkCreateDescriptorPool(device_.device(), &pci, nullptr, &giComputePool_) != VK_SUCCESS)
        throw std::runtime_error("GIVolume: failed to create compute descriptor pool");

    std::array<VkDescriptorSetLayout, 2> layouts = {giComputeSetLayout_, giComputeSetLayout_};
    VkDescriptorSetAllocateInfo ai{};
    ai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    ai.descriptorPool = giComputePool_;
    ai.descriptorSetCount = 2;
    ai.pSetLayouts = layouts.data();
    if (vkAllocateDescriptorSets(device_.device(), &ai, giComputeSets_.data()) != VK_SUCCESS)
        throw std::runtime_error("GIVolume: failed to allocate compute sets");

    // Parity p: write = atlas[p], prev = atlas[1-p].
    for (int p = 0; p < 2; ++p) {
        VkDescriptorBufferInfo rays{raysBuffer_->handle(), 0, VK_WHOLE_SIZE};
        auto imgInfo = [](VkImageView v) {
            VkDescriptorImageInfo i{}; i.imageLayout = VK_IMAGE_LAYOUT_GENERAL; i.imageView = v; return i;
        };
        VkDescriptorImageInfo wi = imgInfo(irradiance_[p]->view());
        VkDescriptorImageInfo wv = imgInfo(visibility_[p]->view());
        VkDescriptorImageInfo pi = imgInfo(irradiance_[1 - p]->view());
        VkDescriptorImageInfo pv = imgInfo(visibility_[1 - p]->view());

        std::array<VkWriteDescriptorSet, 5> w{};
        for (int i = 0; i < 5; ++i) {
            w[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            w[i].dstSet = giComputeSets_[p];
            w[i].dstBinding = i;
            w[i].descriptorCount = 1;
        }
        w[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER; w[0].pBufferInfo = &rays;
        w[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;  w[1].pImageInfo = &wi;
        w[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;  w[2].pImageInfo = &wv;
        w[3].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;  w[3].pImageInfo = &pi;
        w[4].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;  w[4].pImageInfo = &pv;
        vkUpdateDescriptorSets(device_.device(), static_cast<uint32_t>(w.size()), w.data(), 0, nullptr);
    }

    std::vector<VkDescriptorSetLayout> setLayouts = {globalSetLayout, giComputeSetLayout_};
    tracePipeline_  = std::make_unique<ComputePipeline>(device_, shaderPath("ddgi_trace.comp.spv"),  setLayouts, sizeof(TracePush));
    blendPipeline_  = std::make_unique<ComputePipeline>(device_, shaderPath("ddgi_blend.comp.spv"),  setLayouts, sizeof(BlendPush));
    borderPipeline_ = std::make_unique<ComputePipeline>(device_, shaderPath("ddgi_borders.comp.spv"), setLayouts, sizeof(BorderPush));
}

namespace {
// Global memory barrier between compute stages (covers buffer + image memory).
void computeBarrier(VkCommandBuffer cmd, VkPipelineStageFlags2 dstStage) {
    VkMemoryBarrier2 mb{};
    mb.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2;
    mb.srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
    mb.srcAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT;
    mb.dstStageMask = dstStage;
    mb.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT;
    VkDependencyInfo dep{};
    dep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    dep.memoryBarrierCount = 1;
    dep.pMemoryBarriers = &mb;
    vkCmdPipelineBarrier2(cmd, &dep);
}
}

void GIVolume::update(VkCommandBuffer cmd, VkDescriptorSet globalSet, GpuProfiler* profiler) {
    const int probeCount = desc_.probeCount();
    VkDescriptorSet giSet = giComputeSets_[curr_];

    // Make the previous frame's atlas writes (read as "prev") visible to compute.
    computeBarrier(cmd, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);

    // Random rotation of the Fibonacci ray set for temporal coverage.
    std::uniform_real_distribution<float> dist(-1.0f, 1.0f);
    glm::quat q = glm::normalize(glm::quat(dist(rng_), dist(rng_), dist(rng_), dist(rng_)));
    glm::mat4 randomRot = glm::mat4_cast(q);

    // --- 1. Trace ---
    {
        SAIDA_GPU_PROFILE_SCOPE(profiler, cmd, "DDGI/Trace");
        tracePipeline_->bind(cmd);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, tracePipeline_->layout(), 0, 1, &globalSet, 0, nullptr);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, tracePipeline_->layout(), 1, 1, &giSet, 0, nullptr);
        TracePush tp{randomRot, desc_.raysPerProbe, probeCount};
        vkCmdPushConstants(cmd, tracePipeline_->layout(), VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(tp), &tp);
        uint32_t rayGroups = (static_cast<uint32_t>(probeCount * desc_.raysPerProbe) + 63) / 64;
        vkCmdDispatch(cmd, rayGroups, 1, 1);
    }

    computeBarrier(cmd, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);  // rays write -> blend read

    // --- 2. Blend (irradiance then visibility) ---
    {
        SAIDA_GPU_PROFILE_SCOPE(profiler, cmd, "DDGI/Blend");
        blendPipeline_->bind(cmd);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, blendPipeline_->layout(), 0, 1, &globalSet, 0, nullptr);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, blendPipeline_->layout(), 1, 1, &giSet, 0, nullptr);
        auto blend = [&](int mode, glm::ivec2 atlas) {
            BlendPush bp{mode, desc_.raysPerProbe, probeCount, desc_.hysteresis, desc_.distExponent};
            vkCmdPushConstants(cmd, blendPipeline_->layout(), VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(bp), &bp);
            vkCmdDispatch(cmd, (atlas.x + 7) / 8, (atlas.y + 7) / 8, 1);
        };
        blend(0, irradianceAtlasSize());
        blend(1, visibilityAtlasSize());
    }

    computeBarrier(cmd, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);  // blend write -> border read

    // --- 3. Border copy ---
    {
        SAIDA_GPU_PROFILE_SCOPE(profiler, cmd, "DDGI/Borders");
        borderPipeline_->bind(cmd);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, borderPipeline_->layout(), 0, 1, &globalSet, 0, nullptr);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, borderPipeline_->layout(), 1, 1, &giSet, 0, nullptr);
        auto border = [&](int mode, glm::ivec2 atlas) {
            BorderPush bp{mode, probeCount};
            vkCmdPushConstants(cmd, borderPipeline_->layout(), VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(bp), &bp);
            vkCmdDispatch(cmd, (atlas.x + 7) / 8, (atlas.y + 7) / 8, 1);
        };
        border(0, irradianceAtlasSize());
        border(1, visibilityAtlasSize());
    }

    // Border writes -> lighting fragment reads (current atlas, set 0 bindings 4/5).
    computeBarrier(cmd, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT);
}

} // namespace saida
