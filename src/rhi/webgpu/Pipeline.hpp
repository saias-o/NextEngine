#pragma once

#include "rhi/Format.hpp"
#include "rhi/PipelineState.hpp"
#include "rhi/ShaderStages.hpp"
#include "rhi/webgpu/WebGpu.hpp"

#include <memory>
#include <string>
#include <vector>

// WebGPU backend for rhi::Pipeline / rhi::ComputePipeline (Étape 16.4). Same
// Desc shape as the Vulkan backend, with two web-specific realities:
//   - shader paths point at the transpiled .wgsl files (16.2), entry point
//     `main` for every stage;
//   - push constants are emulated: a pipeline with pushConstantSize > 0 gets
//     group 3 = one dynamic-offset uniform slice of the Device's push ring
//     (mirrors `PUSH_QUALIFIER` in shaders/web_compat.glsl).

namespace saida::rhi::webgpu {

class BindGroupLayout;
class Device;

class Pipeline {
public:
    struct Desc {
        std::string vertPath;              // .wgsl
        std::string fragPath;              // empty → depth-only
        std::vector<rhi::Format> colorFormats;
        rhi::Format depthFormat = rhi::Format::Undefined;
        std::vector<const BindGroupLayout*> bindGroupLayouts;  // groups 0..2
        uint32_t samples = 1;
        bool vertexInput = true;           // the engine Vertex layout (8 attributes)
        bool depthTest = true;
        bool depthWrite = true;
        rhi::CompareOp depthCompare = rhi::CompareOp::Less;
        rhi::CullMode cullMode = rhi::CullMode::Back;
        rhi::BlendMode blendMode = rhi::BlendMode::None;
        rhi::Topology topology = rhi::Topology::TriangleList;
        uint32_t pushConstantSize = 0;
        rhi::ShaderStages pushConstantStages = rhi::ShaderStages::VertexFragment;
        uint32_t viewMask = 0;             // ignored: no multiview on web
        // writeMask None on every color target: for passes whose fragment writes
        // nothing (voxelize's dummy attachment — WebGPU forbids attachment-less
        // passes, so a throwaway target keeps the rasterizer running).
        bool colorWrite = true;
        bool depthBias = false;
        float depthBiasConstant = 0.0f;
        float depthBiasSlope = 0.0f;
    };

    Pipeline(Device& device, const Desc& desc);
    ~Pipeline();
    Pipeline(const Pipeline&) = delete;
    Pipeline& operator=(const Pipeline&) = delete;

    WGPURenderPipeline handle() const { return pipeline_; }
    uint32_t pushConstantSize() const { return pushSize_; }
    WGPUBindGroup pushBindGroup() const { return pushGroup_; }  // group 3 slice
    uint32_t groupCountUsed() const { return groupCount_; }     // incl. gaps

private:
    Device& device_;
    WGPURenderPipeline pipeline_ = nullptr;
    WGPUBindGroup pushGroup_ = nullptr;
    WGPUBindGroupLayout pushLayout_ = nullptr;
    uint32_t pushSize_ = 0;
    uint32_t groupCount_ = 0;
};

class ComputePipeline {
public:
    ComputePipeline(Device& device, const std::string& compPath,
                    const std::vector<const BindGroupLayout*>& setLayouts,
                    uint32_t pushConstantSize = 0);
    ~ComputePipeline();
    ComputePipeline(const ComputePipeline&) = delete;
    ComputePipeline& operator=(const ComputePipeline&) = delete;

    WGPUComputePipeline handle() const { return pipeline_; }
    uint32_t pushConstantSize() const { return pushSize_; }
    WGPUBindGroup pushBindGroup() const { return pushGroup_; }

    static uint32_t groupCount(uint32_t total, uint32_t localSize) {
        return (total + localSize - 1) / localSize;
    }

private:
    Device& device_;
    WGPUComputePipeline pipeline_ = nullptr;
    WGPUBindGroup pushGroup_ = nullptr;
    WGPUBindGroupLayout pushLayout_ = nullptr;
    uint32_t pushSize_ = 0;
};

} // namespace saida::rhi::webgpu
