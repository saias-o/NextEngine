#pragma once

#include <vulkan/vulkan.h>

namespace ne {

// Quality tier derived from GPU capabilities.
enum class QualityTier {
    Low,     // mobile / integrated / UMA: no hardware RT, cheapest GI path
    Medium,  // console-class / mid GPU
    High,    // discrete desktop GPU
    Ultra,   // high-end desktop with hardware ray tracing
};

inline const char* toString(QualityTier t) {
    switch (t) {
        case QualityTier::Low:    return "Low";
        case QualityTier::Medium: return "Medium";
        case QualityTier::High:   return "High";
        case QualityTier::Ultra:  return "Ultra";
    }
    return "?";
}

// Features supported and enabled on the selected physical device.
struct RenderCapabilities {
    uint32_t apiVersion = VK_API_VERSION_1_0;  // negotiated device API version

    // Modern core (Vulkan 1.3 / 1.2 / 1.1 features), enabled when supported.
    bool dynamicRendering = false;   // VK 1.3: render without VkRenderPass objects
    bool synchronization2 = false;   // VK 1.3: vkCmdPipelineBarrier2 / submit2
    bool timelineSemaphore = false;  // VK 1.2: graphics <-> async-compute sync
    bool descriptorIndexing = false; // VK 1.2: bindless texture/material arrays
    bool bufferDeviceAddress = false;// VK 1.2: GPU pointers (GPU-driven, BVH)
    bool drawIndirectCount = false;  // VK 1.2: vkCmdDrawIndexedIndirectCount
    bool multiview = false;          // VK 1.1: single-pass stereo (VR)
    bool multiDrawIndirect = false;  // Base feature: DrawIndexedIndirect with drawCount > 1

    // Optional hardware ray tracing (desktop only; detected, not yet enabled).
    bool rayQuery = false;

    // A queue family with compute but not graphics → true async compute.
    bool dedicatedComputeQueue = false;

    bool discreteGpu = false;
    VkSampleCountFlagBits maxSamples = VK_SAMPLE_COUNT_1_BIT;  // capped MSAA
    QualityTier tier = QualityTier::Low;
};

} // namespace ne
