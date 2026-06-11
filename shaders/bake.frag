#version 450
#extension GL_GOOGLE_include_directive : require

// Bakes ONLY the indirect diffuse (GI) from the converged DDGI volume, albedo-free
// (runtime multiplies by surface albedo). Direct lighting + shadows are kept live
// in the scene shader for baked surfaces, so dynamic objects still cast moving
// shadows onto them. Uses the same giIndirectDiffuse() as realtime → consistent.
#include "lighting.glsl"

layout(location = 0) in vec3 fragWorldPos;
layout(location = 1) in vec3 fragNormal;

layout(location = 0) out vec4 outColor;

void main() {
    vec3 N = normalize(fragNormal);
    outColor = vec4(giIndirectDiffuse(fragWorldPos, N, N, vec3(1.0)), 1.0);
}
