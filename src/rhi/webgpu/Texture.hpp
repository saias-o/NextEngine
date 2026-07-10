#pragma once

#include "rhi/Format.hpp"
#include "rhi/webgpu/WebGpu.hpp"

#include <cstdint>
#include <string>

// Mips are generated on the CPU: WebGPU has no image-blit equivalent for uploads.

namespace saida::rhi::webgpu {

class Device;

class Texture {
public:
    Texture(Device& device, const std::string& path, bool srgb = true);
    Texture(Device& device, const uint8_t* pixels, uint32_t width, uint32_t height,
            rhi::Format format = rhi::Format::RGBA8Srgb, bool generateMipmaps = true);
    ~Texture();
    Texture(const Texture&) = delete;
    Texture& operator=(const Texture&) = delete;

    WGPUTextureView imageView() const { return view_; }
    WGPUSampler sampler() const { return sampler_; }
    uint32_t width() const { return width_; }
    uint32_t height() const { return height_; }
    uint32_t bindlessIndex() const { return bindlessIndex_; }
    void setBindlessIndex(uint32_t index) { bindlessIndex_ = index; }

    void updatePixels(const uint8_t* pixels, size_t size);

private:
    void upload(const uint8_t* pixels, bool srgb);

    Device& device_;
    WGPUTexture texture_ = nullptr;
    WGPUTextureView view_ = nullptr;
    WGPUSampler sampler_ = nullptr;
    uint32_t width_ = 0;
    uint32_t height_ = 0;
    uint32_t mipLevels_ = 1;
    bool srgb_ = false;  // mip downsample averages RGB in linear space when true
    uint32_t bindlessIndex_ = ~0u;
};

} // namespace saida::rhi::webgpu
