#version 450

layout(location = 0) in vec2 inUV;
layout(location = 1) in vec4 clipPos;

layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D skyboxTex;

layout(push_constant) uniform PushConsts {
    mat4 invViewProj;
    float exposure;
    float rotation;
} push;

const float INV_TWO_PI = 0.15915494309;
const float INV_PI = 0.31830988618;

void main() {
    // Reconstruct world-space direction from clip-space
    vec4 worldPos = push.invViewProj * vec4(clipPos.xy, 1.0, 1.0);
    vec3 dir = normalize(worldPos.xyz / worldPos.w);
    
    // Apply Y-axis rotation
    float s = sin(push.rotation);
    float c = cos(push.rotation);
    dir.xz = mat2(c, -s, s, c) * dir.xz;
    
    // Spherical mapping to 2D equirectangular UVs
    // Invert dir.y so the sky (positive Y) maps to the top of the texture (V=0)
    vec2 uv = vec2(atan(dir.z, dir.x), asin(-dir.y));
    uv *= vec2(INV_TWO_PI, INV_PI);
    uv += 0.5;
    
    vec3 color = texture(skyboxTex, uv).rgb * push.exposure;
    
    // Write out HDR color. Tonemapping will handle it later.
    outColor = vec4(color, 1.0);
}
