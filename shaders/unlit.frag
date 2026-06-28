#version 450

// Unlit shading model: albedo * baseColor + emissive, written to the HDR target.

// Set 1: per-material data (same layout as shader.frag's non-bindless path).
layout(set = 1, binding = 0) uniform sampler2D texAlbedo;
layout(set = 1, binding = 3) uniform MaterialUBO {
    vec4 baseColor;
    float metallic;
    float roughness;
    float ao;
    float _pad;
    vec4 emissive;
} material;

// Locations match shader.vert.
layout(location = 2) in vec3 fragColor;
layout(location = 3) in vec2 fragTexCoord;

layout(location = 0) out vec4 outColor;

void main() {
    vec4 albedo = texture(texAlbedo, fragTexCoord) * material.baseColor;
    outColor = vec4(albedo.rgb + material.emissive.rgb, albedo.a);
}
