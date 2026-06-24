#pragma once

#include "render/RenderFeature.hpp"
#include "graphics/Pipeline.hpp"

#include <glm/glm.hpp>

#include <memory>

namespace ne {

// Animated water surface — NextEngine's default water. Draws every active WaterNode
// in the scene with a procedural Gerstner grid (no vertex buffer) and Fresnel sky
// reflection. Reuses the global set 0 only; all look/feel rides in a push constant.
// A textbook render feature: zero Renderer edits to add or change it.
class WaterFeature : public ScenePassFeature {
public:
    void createPipelines(const RenderContext& ctx) override;
    void record(const FrameContext& fc) override;

private:
    // 8 vec4 = 128 bytes (the Vulkan guaranteed push-constant minimum). Mirrors
    // WaterPush in water.vert / water.frag.
    struct Push {
        glm::vec4 area;     // x=centerX, y=height, z=centerZ, w=halfSize
        glm::vec4 deep;     // rgb water color, w=roughness
        glm::vec4 shallow;  // rgb foam color, w=reflectivity
        glm::vec4 waveA;    // x=amplitude, y=wavelength, z=speed, w=choppiness
        glm::vec4 detail1;  // x=scale, y=speed, z=strength, w=angleDeg  (normal layer 1)
        glm::vec4 detail2;  // x=scale, y=speed, z=strength, w=angleDeg  (normal layer 2)
        glm::vec4 look;     // x=fresnelPower, y=specularPower, z=specularIntensity, w=foamThreshold
        glm::vec4 misc;     // x=time, y=warpAmount, z=detailFadeDistance, w=foamIntensity
    };
    static constexpr uint32_t kGridRes = 128;  // must match RES in water.vert

    std::unique_ptr<Pipeline> pipeline_;
};

} // namespace ne
