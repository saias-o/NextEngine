#pragma once

#include "rhi/BindGroup.hpp"
#include "rhi/Format.hpp"
#include "rhi/ShaderStages.hpp"
#include "rhi/webgpu/WebGpu.hpp"

#include <vector>

// WebGPU backend for rhi::BindGroup(Layout) (Étape 16.4). WebGPU bind group
// layouts need more static information than Vulkan descriptor layouts (texture
// view dimension, sample type, comparison samplers, dynamic offsets), and the
// web shaders split combined image samplers into texture+sampler pairs with
// their own bindings (shaders/web_compat.glsl). So the web layouts are written
// against the WGSL bindings directly, using this backend's richer entry — the
// divergence anticipated in PLAN_RHI §7.4.

namespace saida::rhi::webgpu {

class Buffer;
class Device;

enum class TextureDim : uint32_t { Dim2D, Dim2DArray, Dim3D, DimCube };

struct BindGroupLayoutEntry {
    uint32_t binding = 0;
    rhi::BindingType type = rhi::BindingType::UniformBuffer;
    rhi::ShaderStages visibility = rhi::ShaderStages::VertexFragment;

    // Texture hints (SampledTexture): dimension + whether the WGSL side reads
    // it as a depth texture or an unfilterable float (e.g. the tonemap depth).
    TextureDim dim = TextureDim::Dim2D;
    bool depthTexture = false;
    bool unfilterable = false;

    // Sampler hints: comparison sampler (shadow PCF) / non-filtering.
    bool comparisonSampler = false;
    bool nonFilteringSampler = false;

    // Buffer hints.
    bool readOnlyStorage = false;
    bool dynamicOffset = false;   // push-constant ring slices

    // StorageImage hint: the declared WGSL storage format.
    rhi::Format storageFormat = rhi::Format::RGBA16Float;
    WGPUStorageTextureAccess storageAccess = WGPUStorageTextureAccess_ReadWrite;
};

class BindGroupLayout {
public:
    BindGroupLayout(Device& device, std::vector<BindGroupLayoutEntry> entries);
    ~BindGroupLayout();
    BindGroupLayout(const BindGroupLayout&) = delete;
    BindGroupLayout& operator=(const BindGroupLayout&) = delete;

    WGPUBindGroupLayout handle() const { return layout_; }
    const std::vector<BindGroupLayoutEntry>& entries() const { return entries_; }
    Device& device() const { return device_; }

private:
    Device& device_;
    std::vector<BindGroupLayoutEntry> entries_;
    WGPUBindGroupLayout layout_ = nullptr;
};

// One bound resource; exactly one of buffer / view / sampler is set.
struct BindGroupEntry {
    uint32_t binding = 0;
    const Buffer* buffer = nullptr;
    uint64_t offset = 0;
    uint64_t range = 0;               // 0 = whole buffer
    WGPUTextureView view = nullptr;   // sampled/storage textures
    WGPUSampler sampler = nullptr;
};

class BindGroup {
public:
    BindGroup(BindGroupLayout& layout, const std::vector<BindGroupEntry>& entries);
    ~BindGroup();
    BindGroup(const BindGroup&) = delete;
    BindGroup& operator=(const BindGroup&) = delete;

    WGPUBindGroup handle() const { return group_; }

private:
    WGPUBindGroup group_ = nullptr;
};

} // namespace saida::rhi::webgpu
