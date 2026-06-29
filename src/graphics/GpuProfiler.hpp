#pragma once

#include "core/Profiler.hpp"

#include <vulkan/vulkan.h>

#include <cstdint>
#include <vector>

namespace ne {

class VulkanDevice;

class GpuProfiler {
public:
    GpuProfiler(VulkanDevice& device, uint32_t framesInFlight, uint32_t maxTimestampsPerFrame = 128);
    ~GpuProfiler();
    GpuProfiler(const GpuProfiler&) = delete;
    GpuProfiler& operator=(const GpuProfiler&) = delete;

    void beginFrame(uint32_t frameIndex);
    void resetQueries(VkCommandBuffer cmd);
    uint32_t beginZone(VkCommandBuffer cmd, const char* name);
    void endZone(VkCommandBuffer cmd, uint32_t zoneHandle);
    void publishLatest();

private:
    struct Zone {
        const char* name = "";
        uint32_t beginQuery = 0;
        uint32_t endQuery = 0;
        uint32_t depth = 0;
    };

    struct FrameState {
        std::vector<Zone> zones;
        uint32_t nextQuery = 0;
    };

    void resolveFrame(uint32_t frameIndex);

    VulkanDevice& device_;
    VkQueryPool queryPool_ = VK_NULL_HANDLE;
    uint32_t framesInFlight_ = 0;
    uint32_t maxTimestampsPerFrame_ = 0;
    uint32_t currentFrame_ = 0;
    float timestampPeriod_ = 1.0f;
    std::vector<FrameState> frames_;
    std::vector<uint32_t> activeStack_;
    std::vector<GpuProfileZone> latestZones_;
};

class GpuProfileScope {
public:
    GpuProfileScope(GpuProfiler* profiler, VkCommandBuffer cmd, const char* name);
    ~GpuProfileScope();
    GpuProfileScope(const GpuProfileScope&) = delete;
    GpuProfileScope& operator=(const GpuProfileScope&) = delete;

private:
    GpuProfiler* profiler_ = nullptr;
    VkCommandBuffer cmd_ = VK_NULL_HANDLE;
    uint32_t handle_ = UINT32_MAX;
};

} // namespace ne

#define NE_GPU_PROFILE_SCOPE(profiler, cmd, name) \
    ::ne::GpuProfileScope NE_PROFILE_CONCAT(_neGpuProfileScope_, __LINE__)((profiler), (cmd), (name))
