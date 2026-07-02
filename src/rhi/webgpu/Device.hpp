#pragma once

#include "rhi/Capabilities.hpp"
#include "rhi/webgpu/WebGpu.hpp"

#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

// WebGPU backend for rhi::Device (Étape 16.4). Mirror of VulkanDevice's
// RHI-facing surface: capabilities(), withSingleTimeEncoder(), waitIdle().
// Creation is asynchronous on the web (requestAdapter/requestDevice callbacks):
// Device::requestAsync hands the ready Device to a continuation, after which
// the app configures the Surface and starts the RAF loop.
//
// The device also owns the push-constant emulation ring: WGSL has no push
// constants, so shaders declare `@group(3) @binding(0) var<uniform> push`
// (see shaders/web_compat.glsl). Each setPushConstants() call allocates a
// 256-byte-aligned slot in a per-frame ring, bound with a dynamic offset.

namespace saida::rhi::webgpu {

class CommandEncoder;

class Device {
public:
    using ReadyFn = std::function<void(std::unique_ptr<Device>)>;

    // Kicks off instance -> adapter -> device; calls `onReady` with the built
    // Device (or nullptr on failure). The caller keeps the runtime alive
    // (emscripten_exit_with_live_runtime) until the callback fires.
    static void requestAsync(ReadyFn onReady);

    ~Device();
    Device(const Device&) = delete;
    Device& operator=(const Device&) = delete;

    WGPUInstance instance() const { return instance_; }
    WGPUDevice device() const { return device_; }
    WGPUQueue queue() const { return queue_; }

    const rhi::Capabilities& capabilities() const { return capabilities_; }

    // One-shot command batch (staging/init). WebGPU has no fences to wait on;
    // the submit is ordered before any later frame submit, which is all the
    // callers rely on.
    void withSingleTimeEncoder(const std::function<void(CommandEncoder&)>& fn);

    void waitIdle() const {}  // driver-managed on web

    // --- Push-constant ring (backend-internal; used by the pass encoders) ---
    // Resets the ring cursor; call once per frame before recording. (Safe
    // because the browser serialises frames: by the next RAF callback the
    // previous submit's reads of the ring are complete.)
    void beginFrame() { pushCursor_ = 0; pushFlushed_ = 0; }
    // Copies `size` bytes into the next aligned slot; returns its byte offset.
    uint32_t allocPushSlot(const void* data, uint32_t size);
    // Uploads this frame's slots; called right before every queue submit.
    void flushPushRing();
    WGPUBuffer pushRingBuffer() const { return pushRing_; }

    // Cached empty bind group layout + group, used to fill pipeline-layout gaps
    // (e.g. [set0, set1, empty, set3-push]) — WebGPU wants contiguous groups.
    WGPUBindGroupLayout emptyBindGroupLayout();
    WGPUBindGroup emptyBindGroup();

    // Backend-internal (used by the requestAsync callbacks); apps go through
    // requestAsync, never construct a Device directly.
    Device() = default;
    void initialize(WGPUInstance instance, WGPUDevice device);

private:

    WGPUInstance instance_ = nullptr;
    WGPUDevice device_ = nullptr;
    WGPUQueue queue_ = nullptr;
    rhi::Capabilities capabilities_{};

    static constexpr uint32_t kPushRingBytes = 256 * 1024;  // 1024 slots/frame
    WGPUBindGroupLayout emptyLayout_ = nullptr;
    WGPUBindGroup emptyGroup_ = nullptr;
    WGPUBuffer pushRing_ = nullptr;
    std::vector<uint8_t> pushStaging_;
    uint32_t pushCursor_ = 0;
    uint32_t pushFlushed_ = 0;
};

} // namespace saida::rhi::webgpu
