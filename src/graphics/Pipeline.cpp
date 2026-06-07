#include "graphics/Pipeline.hpp"

#include "graphics/Mesh.hpp"
#include "graphics/VulkanDevice.hpp"

#include <fstream>
#include <stdexcept>
#include <vector>

namespace ne {

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

} // namespace

Pipeline::Pipeline(VulkanDevice& device, const std::string& vertPath, const std::string& fragPath,
                   const std::vector<VkFormat>& colorFormats, VkFormat depthFormat,
                   const std::vector<VkDescriptorSetLayout>& setLayouts,
                   VkSampleCountFlagBits samples, bool useVertexInput, bool useDepth, uint32_t pushConstantSize,
                   bool depthWrite, VkCompareOp depthCompare, VkCullModeFlags cullMode,
                   bool useBlending, VkPrimitiveTopology topology)
    : device_(device) {
    auto vertCode = readFile(vertPath);
    auto fragCode = readFile(fragPath);
    VkShaderModule vertModule = createShaderModule(vertCode);
    VkShaderModule fragModule = createShaderModule(fragCode);

    VkPipelineShaderStageCreateInfo vertStage{};
    vertStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vertStage.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vertStage.module = vertModule;
    vertStage.pName = "main";

    VkPipelineShaderStageCreateInfo fragStage{};
    fragStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    fragStage.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    fragStage.module = fragModule;
    fragStage.pName = "main";

    VkPipelineShaderStageCreateInfo stages[] = {vertStage, fragStage};

    auto bindingDesc = Vertex::bindingDescription();
    auto attrDescs = Vertex::attributeDescriptions();

    VkPipelineVertexInputStateCreateInfo vertexInput{};
    vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    if (useVertexInput) {
        vertexInput.vertexBindingDescriptionCount = 1;
        vertexInput.pVertexBindingDescriptions = &bindingDesc;
        vertexInput.vertexAttributeDescriptionCount = static_cast<uint32_t>(attrDescs.size());
        vertexInput.pVertexAttributeDescriptions = attrDescs.data();
    }

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = topology;
    inputAssembly.primitiveRestartEnable = VK_FALSE;

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.depthClampEnable = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = cullMode;
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.depthBiasEnable = VK_FALSE;

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.rasterizationSamples = samples;

    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = useDepth ? VK_TRUE : VK_FALSE;
    depthStencil.depthWriteEnable = (useDepth && depthWrite) ? VK_TRUE : VK_FALSE;
    depthStencil.depthCompareOp = depthCompare;
    depthStencil.depthBoundsTestEnable = VK_FALSE;
    depthStencil.stencilTestEnable = VK_FALSE;

    VkPipelineColorBlendAttachmentState colorBlendAttach{};
    colorBlendAttach.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT
                                    | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    colorBlendAttach.blendEnable = useBlending ? VK_TRUE : VK_FALSE;
    if (useBlending) {
        colorBlendAttach.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        colorBlendAttach.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        colorBlendAttach.colorBlendOp = VK_BLEND_OP_ADD;
        colorBlendAttach.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        colorBlendAttach.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
        colorBlendAttach.alphaBlendOp = VK_BLEND_OP_ADD;
    }

    VkPipelineColorBlendStateCreateInfo colorBlend{};
    colorBlend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlend.logicOpEnable = VK_FALSE;
    colorBlend.attachmentCount = 1;
    colorBlend.pAttachments = &colorBlendAttach;

    std::vector<VkDynamicState> dynamicStates = {
        VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR
    };
    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
    dynamicState.pDynamicStates = dynamicStates.data();

    // Per-object push constants: model matrix (vertex) + params (fragment).
    // Matches PushConstants in shader.vert / shader.frag (mat4 + vec4 = 80 bytes).
    VkPushConstantRange pushRange{};
    pushRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    pushRange.offset = 0;
    pushRange.size = pushConstantSize > 0 ? pushConstantSize : (sizeof(glm::mat4) + sizeof(glm::vec4));

    VkPipelineLayoutCreateInfo layoutCI{};
    layoutCI.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutCI.setLayoutCount = static_cast<uint32_t>(setLayouts.size());
    layoutCI.pSetLayouts = setLayouts.data();
    layoutCI.pushConstantRangeCount = 1;
    layoutCI.pPushConstantRanges = &pushRange;

    if (vkCreatePipelineLayout(device_.device(), &layoutCI, nullptr, &layout_) != VK_SUCCESS)
        throw std::runtime_error("failed to create pipeline layout");

    // Dynamic rendering: declare the attachment formats instead of a render pass.
    VkPipelineRenderingCreateInfo renderingInfo{};
    renderingInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    renderingInfo.colorAttachmentCount = static_cast<uint32_t>(colorFormats.size());
    renderingInfo.pColorAttachmentFormats = colorFormats.empty() ? nullptr : colorFormats.data();
    renderingInfo.depthAttachmentFormat = depthFormat;

    VkGraphicsPipelineCreateInfo pipelineCI{};
    pipelineCI.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineCI.pNext = &renderingInfo;
    pipelineCI.stageCount = 2;
    pipelineCI.pStages = stages;
    pipelineCI.pVertexInputState = &vertexInput;
    pipelineCI.pInputAssemblyState = &inputAssembly;
    pipelineCI.pViewportState = &viewportState;
    pipelineCI.pRasterizationState = &rasterizer;
    pipelineCI.pMultisampleState = &multisampling;
    pipelineCI.pDepthStencilState = &depthStencil;
    pipelineCI.pColorBlendState = &colorBlend;
    pipelineCI.pDynamicState = &dynamicState;
    pipelineCI.layout = layout_;
    pipelineCI.renderPass = VK_NULL_HANDLE;
    pipelineCI.subpass = 0;

    if (vkCreateGraphicsPipelines(device_.device(), device_.pipelineCache(), 1, &pipelineCI, nullptr,
        &pipeline_) != VK_SUCCESS)
        throw std::runtime_error("failed to create graphics pipeline");

    vkDestroyShaderModule(device_.device(), fragModule, nullptr);
    vkDestroyShaderModule(device_.device(), vertModule, nullptr);
}

Pipeline::~Pipeline() {
    vkDestroyPipeline(device_.device(), pipeline_, nullptr);
    vkDestroyPipelineLayout(device_.device(), layout_, nullptr);
}

VkShaderModule Pipeline::createShaderModule(const std::vector<char>& code) const {
    VkShaderModuleCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    ci.codeSize = code.size();
    ci.pCode = reinterpret_cast<const uint32_t*>(code.data());
    VkShaderModule mod;
    if (vkCreateShaderModule(device_.device(), &ci, nullptr, &mod) != VK_SUCCESS)
        throw std::runtime_error("failed to create shader module");
    return mod;
}

void Pipeline::bind(VkCommandBuffer cmd) const {
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_);
}

} // namespace ne
