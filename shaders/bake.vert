#version 450

// Lightmap bake: rasterize the mesh in *lightmap UV space* (each vertex placed at
// its lightmapUV) while carrying world position + normal to the fragment stage,
// so the bake fragment shader can evaluate world-space lighting per texel.
layout(push_constant) uniform PushConstants {
    mat4 model;
    vec4 params;  // unused here; layout shared with the scene shader
} push;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec3 inColor;
layout(location = 3) in vec2 inTexCoord;
layout(location = 4) in vec2 inLightmapUV;

layout(location = 0) out vec3 fragWorldPos;
layout(location = 1) out vec3 fragNormal;

void main() {
    vec4 worldPos = push.model * vec4(inPosition, 1.0);
    fragWorldPos = worldPos.xyz;
    fragNormal = mat3(transpose(inverse(push.model))) * inNormal;
    // Map the [0,1] lightmap UV to clip space [-1,1] so each texel is shaded once.
    gl_Position = vec4(inLightmapUV * 2.0 - 1.0, 0.0, 1.0);
}
