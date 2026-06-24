#include "scene/WaterNode.hpp"

namespace ne {

void WaterNode::describe(reflect::TypeBuilder<WaterNode>& t) {
    t.doc("Animated water plane (default NextEngine water). Position = centre/height, "
          "size = half-extent. Procedural waves + Fresnel sky reflection, no textures.");
    t.property("size", &WaterNode::size).range(1.0, 5000.0).tooltip("half-extent of the plane (m)");
    t.property("deepColor", &WaterNode::deepColor).tooltip("refracted body tint");
    t.property("foamColor", &WaterNode::foamColor).tooltip("wave-crest foam colour");
    t.property("roughness", &WaterNode::roughness).range(0.02, 0.4)
        .tooltip("reflection sharpness (low = mirror-like)");
    t.property("reflectivity", &WaterNode::reflectivity).range(0.0, 2.0)
        .tooltip("Fresnel sky-reflection strength");

    t.property("amplitude", &WaterNode::amplitude).range(0.0, 5.0).tooltip("wave height (m)");
    t.property("wavelength", &WaterNode::wavelength).range(0.5, 200.0)
        .tooltip("distance between primary crests (m)");
    t.property("waveSpeed", &WaterNode::waveSpeed).range(0.0, 10.0);
    t.property("choppiness", &WaterNode::choppiness).range(0.0, 1.5)
        .tooltip("crest sharpening (horizontal pull)");

    t.property("detailScale", &WaterNode::detailScale).range(0.02, 5.0)
        .tooltip("normal layer 1: feature frequency");
    t.property("detailSpeed", &WaterNode::detailSpeed).range(0.0, 2.0).tooltip("layer 1 flow speed");
    t.property("detailStrength", &WaterNode::detailStrength).range(0.0, 3.0).tooltip("layer 1 intensity");
    t.property("detailAngle", &WaterNode::detailAngle).range(0.0, 360.0).tooltip("layer 1 flow direction (deg)");
    t.property("detail2Scale", &WaterNode::detail2Scale).range(0.02, 5.0)
        .tooltip("normal layer 2: feature frequency");
    t.property("detail2Speed", &WaterNode::detail2Speed).range(0.0, 2.0).tooltip("layer 2 flow speed");
    t.property("detail2Strength", &WaterNode::detail2Strength).range(0.0, 3.0).tooltip("layer 2 intensity");
    t.property("detail2Angle", &WaterNode::detail2Angle).range(0.0, 360.0).tooltip("layer 2 flow direction (deg)");

    t.property("fresnelPower", &WaterNode::fresnelPower).range(1.0, 8.0)
        .tooltip("higher = reflection only at grazing angles");
    t.property("specularPower", &WaterNode::specularPower).range(8.0, 2000.0)
        .tooltip("sun sparkle tightness");
    t.property("specularIntensity", &WaterNode::specularIntensity).range(0.0, 5.0);
    t.property("foamThreshold", &WaterNode::foamThreshold).range(0.0, 1.0)
        .tooltip("crest height where foam starts");
    t.property("foamIntensity", &WaterNode::foamIntensity).range(0.0, 1.0);
    t.property("warpAmount", &WaterNode::warpAmount).range(0.0, 6.0)
        .tooltip("domain warp that breaks large-scale tiling");
    t.property("detailFadeDistance", &WaterNode::detailFadeDistance).range(10.0, 2000.0)
        .tooltip("ripples calm down past this distance");
}

} // namespace ne
