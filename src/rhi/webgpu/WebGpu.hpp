#pragma once

// Common WebGPU includes/helpers for the rhi::webgpu backend (Étape 16.4).
// Built ONLY by the Emscripten web target (web/runtime) — never part of the
// desktop build, which stays on rhi/vulkan. Uses the emdawnwebgpu port
// (`--use-port=emdawnwebgpu`), i.e. the current webgpu.h with WGPUStringView
// and callback-info style async entry points (same API as web/spike).

#include <webgpu/webgpu.h>

#include <cstdint>

namespace saida::rhi::webgpu {

inline WGPUStringView sv(const char* s) {
    WGPUStringView v;
    v.data = s;
    v.length = WGPU_STRLEN;
    return v;
}

// Push-constant emulation slots are dynamic-offset uniform slices; WebGPU
// requires 256-byte alignment for dynamic offsets.
inline constexpr uint32_t kPushSlotAlignment = 256;

} // namespace saida::rhi::webgpu
