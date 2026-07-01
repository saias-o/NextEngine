#pragma once

#include "rhi/Format.hpp"
#include "rhi/PipelineState.hpp"
#include "rhi/ShaderStages.hpp"

#include <vulkan/vulkan.h>

#include <string>
#include <vector>

namespace saida::rhi::vulkan {
class BindGroupLayout;
}

namespace saida {

class VulkanDevice;

// BlendMode moved to the neutral rhi layer (16.3.d); aliased back so existing
// call sites keep writing saida::BlendMode.
using BlendMode = rhi::BlendMode;

// A graphics pipeline configured for the Vertex layout, built against dynamic
// rendering attachment formats (no VkRenderPass) and descriptor set layouts.
class Pipeline {
public:
    // Backend-neutral construction (Étape 16.3.d). The WebGPU backend's Pipeline
    // (16.4) exposes the same Desc shape with its own BindGroupLayout type.
    struct Desc {
        std::string vertPath;
        std::string fragPath;  // empty → depth-only pipeline (no fragment stage)
        std::vector<rhi::Format> colorFormats;
        rhi::Format depthFormat = rhi::Format::Undefined;
        std::vector<const rhi::vulkan::BindGroupLayout*> bindGroupLayouts;
        uint32_t samples = 1;
        bool vertexInput = true;
        bool depthTest = true;
        bool depthWrite = true;
        rhi::CompareOp depthCompare = rhi::CompareOp::Less;
        rhi::CullMode cullMode = rhi::CullMode::Back;
        rhi::BlendMode blendMode = rhi::BlendMode::None;
        rhi::Topology topology = rhi::Topology::TriangleList;
        // 0 keeps the legacy default range (mat4 model + vec4 params = 80 bytes).
        uint32_t pushConstantSize = 0;
        rhi::ShaderStages pushConstantStages = rhi::ShaderStages::VertexFragment;
        uint32_t viewMask = 0;  // multiview (XR stereo); 0 = single view
        bool depthBias = false; // shadow acne bias (depth-only passes)
        float depthBiasConstant = 0.0f;
        float depthBiasSlope = 0.0f;
    };

    Pipeline(VulkanDevice& device, const Desc& desc);

    // Legacy Vulkan-typed constructors; converted site-by-site to Desc (16.3.d.c),
    // then removed.
    Pipeline(VulkanDevice& device, const std::string& vertPath, const std::string& fragPath,
             const std::vector<VkFormat>& colorFormats, VkFormat depthFormat,
             const std::vector<VkDescriptorSetLayout>& setLayouts,
             VkSampleCountFlagBits samples, bool useVertexInput = true, bool useDepth = true, uint32_t pushConstantSize = 0,
             bool depthWrite = true, VkCompareOp depthCompare = VK_COMPARE_OP_LESS, VkCullModeFlags cullMode = VK_CULL_MODE_BACK_BIT,
             bool useBlending = false, VkPrimitiveTopology topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
             uint32_t viewMask = 0);
    Pipeline(VulkanDevice& device, const std::string& vertPath, const std::string& fragPath,
             const std::vector<VkFormat>& colorFormats, VkFormat depthFormat,
             const std::vector<VkDescriptorSetLayout>& setLayouts,
             VkSampleCountFlagBits samples, bool useVertexInput, bool useDepth, uint32_t pushConstantSize,
             bool depthWrite, VkCompareOp depthCompare, VkCullModeFlags cullMode,
             BlendMode blendMode, VkPrimitiveTopology topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
             uint32_t viewMask = 0);
    ~Pipeline();
    Pipeline(const Pipeline&) = delete;
    Pipeline& operator=(const Pipeline&) = delete;

    void bind(VkCommandBuffer cmd) const;
    VkPipelineLayout layout() const { return layout_; }
    // Stages of the pipeline's push constant range; RenderPassEncoder uses them
    // so setPushConstants needs no stage parameter.
    VkShaderStageFlags pushConstantStages() const { return pushStages_; }

private:
    // Internal Vk-typed build parameters. The legacy constructors fill this
    // as-is (zero behaviour change); the Desc constructor maps neutral → Vk.
    struct BuildInfo {
        std::string vertPath;
        std::string fragPath;  // empty → no fragment stage
        std::vector<VkFormat> colorFormats;
        VkFormat depthFormat = VK_FORMAT_UNDEFINED;
        std::vector<VkDescriptorSetLayout> setLayouts;
        VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT;
        bool useVertexInput = true;
        bool useDepth = true;
        uint32_t pushConstantSize = 0;
        bool depthWrite = true;
        VkCompareOp depthCompare = VK_COMPARE_OP_LESS;
        VkCullModeFlags cullMode = VK_CULL_MODE_BACK_BIT;
        BlendMode blendMode = BlendMode::None;
        VkPrimitiveTopology topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        uint32_t viewMask = 0;
        VkShaderStageFlags pushStages = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
        bool depthBias = false;
        float depthBiasConstant = 0.0f;
        float depthBiasSlope = 0.0f;
    };

    void build(const BuildInfo& info);
    VkShaderModule createShaderModule(const std::vector<char>& code) const;

    VulkanDevice& device_;
    VkPipelineLayout layout_ = VK_NULL_HANDLE;
    VkPipeline pipeline_ = VK_NULL_HANDLE;
    VkShaderStageFlags pushStages_ = 0;
};

} // namespace saida
