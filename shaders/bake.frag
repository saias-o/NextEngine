#version 450
#extension GL_GOOGLE_include_directive : require

// Same lighting math as the realtime shader → the bake equals realtime exactly.
// We bake only the view-independent diffuse irradiance (ambient + diffuse +
// shadows); specular is left to the realtime path.
#include "lighting.glsl"

layout(location = 0) in vec3 fragWorldPos;
layout(location = 1) in vec3 fragNormal;

layout(location = 0) out vec4 outColor;

void main() {
    vec3 N = normalize(fragNormal);
    outColor = vec4(diffuseIrradiance(N, fragWorldPos, vec3(1.0), 0.0, 0.5), 1.0);
}
