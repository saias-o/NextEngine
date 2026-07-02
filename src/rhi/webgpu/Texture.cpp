#include "rhi/webgpu/Texture.hpp"

#include "rhi/webgpu/Device.hpp"
#include "rhi/webgpu/Format.hpp"

#include <stb_image.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>
#include <vector>

namespace saida::rhi::webgpu {

namespace {

// Simple sRGB-agnostic box downsample; visually fine for asset mips.
std::vector<uint8_t> downsample(const std::vector<uint8_t>& src, uint32_t w, uint32_t h,
                                uint32_t nw, uint32_t nh) {
    std::vector<uint8_t> dst(size_t(nw) * nh * 4);
    for (uint32_t y = 0; y < nh; ++y) {
        for (uint32_t x = 0; x < nw; ++x) {
            const uint32_t sx = std::min(x * 2, w - 1), sy = std::min(y * 2, h - 1);
            const uint32_t sx1 = std::min(sx + 1, w - 1), sy1 = std::min(sy + 1, h - 1);
            for (int c = 0; c < 4; ++c) {
                const uint32_t sum = src[(size_t(sy) * w + sx) * 4 + c] +
                                     src[(size_t(sy) * w + sx1) * 4 + c] +
                                     src[(size_t(sy1) * w + sx) * 4 + c] +
                                     src[(size_t(sy1) * w + sx1) * 4 + c];
                dst[(size_t(y) * nw + x) * 4 + c] = uint8_t(sum / 4);
            }
        }
    }
    return dst;
}

} // namespace

Texture::Texture(Device& device, const std::string& path, bool srgb)
    : device_(device) {
    int texWidth = 0, texHeight = 0, channels = 0;
    stbi_uc* pixels = stbi_load(path.c_str(), &texWidth, &texHeight, &channels, STBI_rgb_alpha);
    if (!pixels)
        throw std::runtime_error("webgpu: failed to load texture '" + path + "'");

    Texture loaded(device, pixels, static_cast<uint32_t>(texWidth),
                   static_cast<uint32_t>(texHeight),
                   srgb ? rhi::Format::RGBA8Srgb : rhi::Format::RGBA8Unorm);
    stbi_image_free(pixels);

    texture_ = loaded.texture_;
    view_ = loaded.view_;
    sampler_ = loaded.sampler_;
    width_ = loaded.width_;
    height_ = loaded.height_;
    mipLevels_ = loaded.mipLevels_;

    loaded.texture_ = nullptr;
    loaded.view_ = nullptr;
    loaded.sampler_ = nullptr;
}

Texture::Texture(Device& device, const uint8_t* pixels, uint32_t width, uint32_t height,
                 rhi::Format format, bool generateMipmaps)
    : device_(device), width_(width), height_(height) {
    mipLevels_ = 1;
    if (generateMipmaps && (width > 1 || height > 1))
        mipLevels_ = uint32_t(std::floor(std::log2(std::max(width, height)))) + 1;

    WGPUTextureDescriptor desc = {};
    desc.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst;
    desc.dimension = WGPUTextureDimension_2D;
    desc.size = {width_, height_, 1};
    desc.format = toWgpu(format);
    desc.mipLevelCount = mipLevels_;
    desc.sampleCount = 1;
    texture_ = wgpuDeviceCreateTexture(device_.device(), &desc);
    if (!texture_) throw std::runtime_error("webgpu: failed to create texture");

    upload(pixels);

    WGPUTextureViewDescriptor vd = {};
    vd.format = desc.format;
    vd.dimension = WGPUTextureViewDimension_2D;
    vd.mipLevelCount = mipLevels_;
    vd.arrayLayerCount = 1;
    view_ = wgpuTextureCreateView(texture_, &vd);

    WGPUSamplerDescriptor sd = {};
    sd.addressModeU = WGPUAddressMode_Repeat;
    sd.addressModeV = WGPUAddressMode_Repeat;
    sd.addressModeW = WGPUAddressMode_Repeat;
    sd.magFilter = WGPUFilterMode_Linear;
    sd.minFilter = WGPUFilterMode_Linear;
    sd.mipmapFilter = WGPUMipmapFilterMode_Linear;
    sd.lodMaxClamp = float(mipLevels_);
    sd.maxAnisotropy = 1;
    sampler_ = wgpuDeviceCreateSampler(device_.device(), &sd);
}

void Texture::upload(const uint8_t* pixels) {
    std::vector<uint8_t> level(pixels, pixels + size_t(width_) * height_ * 4);
    uint32_t w = width_, h = height_;
    for (uint32_t mip = 0; mip < mipLevels_; ++mip) {
        WGPUTexelCopyTextureInfo dst = {};
        dst.texture = texture_;
        dst.mipLevel = mip;
        WGPUTexelCopyBufferLayout layout = {};
        layout.bytesPerRow = w * 4;
        layout.rowsPerImage = h;
        WGPUExtent3D extent = {w, h, 1};
        wgpuQueueWriteTexture(device_.queue(), &dst, level.data(), level.size(), &layout, &extent);

        if (mip + 1 < mipLevels_) {
            const uint32_t nw = std::max(1u, w / 2), nh = std::max(1u, h / 2);
            level = downsample(level, w, h, nw, nh);
            w = nw;
            h = nh;
        }
    }
}

void Texture::updatePixels(const uint8_t* pixels, size_t size) {
    (void)size;
    upload(pixels);
}

Texture::~Texture() {
    if (sampler_) wgpuSamplerRelease(sampler_);
    if (view_) wgpuTextureViewRelease(view_);
    if (texture_) {
        wgpuTextureDestroy(texture_);
        wgpuTextureRelease(texture_);
    }
}

} // namespace saida::rhi::webgpu
