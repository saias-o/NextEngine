#version 450

// Unlit shading model — albedo * baseColor + emissive, no lighting.
//
// Shares the scene vertex shader (shader.vert / multiview.shader.vert) and the
// EXACT set 0/1/2 + push-constant layout of the Lit pipeline, so the Renderer
// swaps to this pipeline per material (MaterialType::Unlit) with zero other
// changes. Only the bindings actually read here are declared; the unused set-0
// (lighting, shadows, GI) and set-2 (lightmap) bindings stay out of this stage,
// which is exactly why unlit is cheaper than Lit.
//
// Outputs LINEAR color into the shared HDR target; the common tonemap pass then
// applies exposure/encoding just like every other surface, so unlit and lit
// objects sit in the same color space.

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

// Interpolated vertex outputs (locations match shader.vert). Unused inputs from
// the vertex stage (world pos, normal, lightmap UV) are simply not declared.
layout(location = 2) in vec3 fragColor;
layout(location = 3) in vec2 fragTexCoord;

layout(location = 0) out vec4 outColor;

void main() {
    vec4 albedo = texture(texAlbedo, fragTexCoord) * material.baseColor;
    outColor = vec4(albedo.rgb + material.emissive.rgb, albedo.a);
}
