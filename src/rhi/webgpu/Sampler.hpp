#pragma once

#include "rhi/Sampler.hpp"
#include "rhi/webgpu/WebGpu.hpp"

// WebGPU backend for rhi::Sampler (Étape 16.4).

namespace saida::rhi::webgpu {

class Device;

class Sampler {
public:
    Sampler(Device& device, const rhi::SamplerDesc& desc);
    ~Sampler();
    Sampler(const Sampler&) = delete;
    Sampler& operator=(const Sampler&) = delete;

    WGPUSampler handle() const { return sampler_; }

private:
    WGPUSampler sampler_ = nullptr;
};

} // namespace saida::rhi::webgpu
