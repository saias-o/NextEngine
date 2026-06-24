#pragma once

#include "render/RenderFeature.hpp"
#include "graphics/Pipeline.hpp"

#include <glm/glm.hpp>

#include <memory>
#include <vector>

namespace ne {

class VulkanDevice;
class Buffer;

// Animated water surface — NextEngine's default water. Draws every active WaterNode
// in the scene with a procedural Gerstner grid (no vertex buffer) and Fresnel sky
// reflection, plus an optional analytic shore (beach/lake) with a depth-tinted body,
// swash foam and a soft waterline. Reuses the global set 0; per-node look/feel rides
// in a small set-1 UBO array (indexed by a push constant). A textbook render feature:
// zero Renderer edits to add or change it.
class WaterFeature : public ScenePassFeature {
public:
    ~WaterFeature() override;
    void createPipelines(const RenderContext& ctx) override;
    void record(const FrameContext& fc) override;

private:
    // Mirrors GpuWater in water_common.glsl (12 vec4, std140 — all-vec4 so the layout
    // is identical on both sides with no padding). Per WaterNode, per frame.
    struct GpuWater {
        glm::vec4 area;        // centreX, surfaceY, centreZ, halfSize
        glm::vec4 deep;        // rgb deep colour, w roughness
        glm::vec4 foam;        // rgb foam colour, w reflectivity
        glm::vec4 waveA;       // amp, wavelength, speed, choppiness
        glm::vec4 detail1;     // scale, speed, strength, angleDeg
        glm::vec4 detail2;     // scale, speed, strength, angleDeg
        glm::vec4 look;        // fresnelPower, specularPower, specularIntensity, foamThreshold
        glm::vec4 misc;        // warpAmount, detailFadeDistance, foamIntensity, depthColorFalloff
        glm::vec4 shoreColor;  // rgb shallow colour, w edgeFadeDepth
        glm::vec4 shoreGeom;   // beach(dirX,dirZ,waterlineDist,slope) | lake(cx,cz,radius,slope)
        glm::vec4 shoreTune;   // foamWidth, swashSpeed, swashAmount, waveFlattenDepth
        glm::vec4 shoreMode;   // mode, shoreFoamIntensity, reserved, reserved
    };

    // Tiny per-draw push: which water entry + the animation clock.
    struct Push {
        uint32_t index;
        float time;
    };

    static constexpr uint32_t kGridRes = 128;  // must match RES in water.vert
    static constexpr uint32_t kMaxWaters = 8;  // must match WATER_MAX in water_common.glsl

    VulkanDevice* device_ = nullptr;
    std::unique_ptr<Pipeline> pipeline_;

    // set 1: a UBO array of GpuWater, double-buffered per frame-in-flight.
    VkDescriptorSetLayout setLayout_ = VK_NULL_HANDLE;
    VkDescriptorPool pool_ = VK_NULL_HANDLE;
    std::vector<std::unique_ptr<Buffer>> ubos_;     // one per frame-in-flight
    std::vector<VkDescriptorSet> sets_;             // one per frame-in-flight
};

} // namespace ne
