#pragma once

#include "scene/Node.hpp"
#include "core/Reflection.hpp"

#include <glm/glm.hpp>

namespace ne {

// A large animated water plane, rendered by the engine's dedicated water pipeline
// (procedural Gerstner-style waves + Fresnel sky reflection — no textures, so it
// stays VR/mobile friendly). This is NextEngine's default water for games.
//
// The node's world position is the plane's centre and height; `size` is its
// half-extent in metres. Every look/feel knob is reflected, so it is inspectable,
// serialized, and exposed in the LLM manifest — drop a "Water" node in a scene and
// tune it, no code required.
class WaterNode : public Node {
public:
    WaterNode() : Node("Water") {}

    NE_REFLECT_NODE(WaterNode, "Water")

    // Defaults are the values tuned by eye in the GTAClone scene, rounded to clean
    // numbers — except the speeds, kept at their earlier (better) values.
    float size = 300.0f;          // half-extent of the plane (m)

    glm::vec3 deepColor{0.01f, 0.07f, 0.11f};   // refracted body tint
    glm::vec3 foamColor{0.80f, 0.90f, 0.96f};   // crest foam colour
    float roughness = 0.05f;       // reflection sharpness (low = mirror-like)
    float reflectivity = 0.55f;    // strength of the Fresnel sky reflection

    // Big rolling waves (vertex displacement). Several octaves with rotated,
    // non-harmonic frequencies, so the swell never reads as a repeating grid.
    float amplitude = 0.25f;       // wave height (m)
    float wavelength = 15.0f;      // distance between primary crests (m)
    float waveSpeed = 0.7f;        // scroll speed (kept from before)
    float choppiness = 0.25f;      // crest sharpening (Gerstner-like horizontal pull)

    // Two procedural normal layers (the modern "two scrolling normal maps", but
    // built from rotated FBM value-noise so there is NO texture tiling). Different
    // scale / speed / direction on each layer breaks up repetition.
    float detailScale = 0.7f;      // layer 1: feature frequency (×world metres)
    float detailSpeed = 0.06f;     // layer 1: flow speed (kept from before)
    float detailStrength = 0.2f;   // layer 1: ripple intensity
    float detailAngle = 100.0f;    // layer 1: flow direction (deg)
    float detail2Scale = 0.9f;     // layer 2: finer features
    float detail2Speed = 0.12f;    // layer 2: flow speed (kept from before)
    float detail2Strength = 0.35f; // layer 2: ripple intensity
    float detail2Angle = 110.0f;   // layer 2: flow direction (deg)

    // Look / shading.
    float fresnelPower = 5.0f;     // higher = reflection only at grazing angles
    float specularPower = 35.0f;   // sun sparkle tightness
    float specularIntensity = 0.25f;
    float foamThreshold = 0.2f;    // crest height where foam starts (0..1)
    float foamIntensity = 0.06f;   // how white the foam gets

    float warpAmount = 0.1f;       // domain-warp that breaks large-scale regularity
    float detailFadeDistance = 200.0f;  // ripples calm down past this (anti-shimmer)
};

} // namespace ne
