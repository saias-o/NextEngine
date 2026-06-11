#version 450
#extension GL_GOOGLE_include_directive : require

#include "lighting.glsl"

#ifdef BINDLESS

struct MaterialData {
    vec4 baseColor;
    float metallic;
    float roughness;
    float ao;
    uint albedoTexIdx;
    uint normalTexIdx;
    uint metallicRoughnessTexIdx;
    uint emissiveTexIdx;
    uint pad;
    vec4 emissive;
};

// Requires descriptor indexing features
#extension GL_EXT_nonuniform_qualifier : require

layout(set = 1, binding = 0) uniform sampler2D globalTextures[8192];

layout(std430, set = 1, binding = 1) readonly buffer MaterialBuffer {
    MaterialData materials[];
};

#else

// Set 1: per-material data.
layout(set = 1, binding = 0) uniform sampler2D texAlbedo;
layout(set = 1, binding = 1) uniform sampler2D texNormal;
layout(set = 1, binding = 2) uniform sampler2D texMetallicRoughness;
layout(set = 1, binding = 3) uniform MaterialUBO {
    vec4 baseColor;
    float metallic;
    float roughness;
    float ao;
    float _pad;
    vec4 emissive;
} material;
layout(set = 1, binding = 4) uniform sampler2D texEmissive;

#endif

// Set 2: per-object baked lightmap (default 1x1 white when not baked).
layout(set = 2, binding = 0) uniform sampler2D lightmap;

layout(push_constant) uniform PushConstants {
    mat4 model;
    vec4 params;  // x = useLightmap (>0.5 → sample baked diffuse)
} push;

layout(location = 0) in vec3 fragWorldPos;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec3 fragColor;
layout(location = 3) in vec2 fragTexCoord;
layout(location = 4) in vec2 fragLightmapUV;
layout(location = 5) in vec3 fragTangent;
layout(location = 6) in vec3 fragBitangent;

#ifdef BINDLESS
layout(location = 7) flat in uint fragMaterialIndex;
#endif

layout(location = 0) out vec4 outColor;

void main() {
#ifdef BINDLESS
    MaterialData mat = materials[fragMaterialIndex];
    vec4 baseColor = mat.baseColor;
    float matMetallic = mat.metallic;
    float matRoughness = mat.roughness;
    float matAO = mat.ao;
    vec4 matEmissive = mat.emissive;
    vec4 albedoSample = texture(globalTextures[nonuniformEXT(mat.albedoTexIdx)], fragTexCoord);
#else
    vec4 baseColor = material.baseColor;
    float matMetallic = material.metallic;
    float matRoughness = material.roughness;
    float matAO = material.ao;
    vec4 matEmissive = material.emissive;
    vec4 albedoSample = texture(texAlbedo, fragTexCoord);
#endif

    vec3 albedo = albedoSample.rgb * fragColor * baseColor.rgb;

    // GI debug: visualize the voxelized scene albedo (giAtlas.z == 1). Snap the
    // sample to the voxel center so neighbouring fragments in the same voxel get
    // the same color — making the grid visible as discrete blocks (and exposing
    // any grid misalignment / missing coverage).
    if (lights.giAtlas.z == 1) {
        float res = float(lights.giAtlas.w);
        vec3 uvw = giVolumeUVW(fragWorldPos);
        vec3 snapped = (floor(uvw * res) + 0.5) / res;
        outColor = vec4(texture(giVoxels, snapped).rgb, 1.0);
        return;
    }

    vec3 N = normalize(fragNormal);
    vec3 T = normalize(fragTangent);
    vec3 B = normalize(fragBitangent);
    mat3 TBN = mat3(T, B, N);

#ifdef BINDLESS
    vec3 normalSample = texture(globalTextures[nonuniformEXT(mat.normalTexIdx)], fragTexCoord).xyz;
#else
    vec3 normalSample = texture(texNormal, fragTexCoord).xyz;
#endif
    if (abs(normalSample.x - 0.5) > 0.01 || abs(normalSample.y - 0.5) > 0.01) {
        vec3 tangentNormal = normalSample * 2.0 - 1.0;
        N = normalize(TBN * tangentNormal);
    }

#ifdef BINDLESS
    vec3 mrSample = texture(globalTextures[nonuniformEXT(mat.metallicRoughnessTexIdx)], fragTexCoord).rgb;
#else
    vec3 mrSample = texture(texMetallicRoughness, fragTexCoord).rgb;
#endif
    float roughness = mrSample.g * matRoughness;
    float metallic = mrSample.b * matMetallic;

    vec3 V = normalize(lights.cameraPos.xyz - fragWorldPos);

    // Single unified path: indirect diffuse from the DDGI volume (live in realtime,
    // frozen in baked mode) + live direct lighting + live shadows. No per-mesh
    // lightmaps — every surface (static or dynamic) samples the same volume, so
    // baked == realtime visually and there are no lightmap-UV issues.
    LightTerms t = accumulate(N, V, fragWorldPos, albedo, metallic, roughness);
    vec3 lit = t.diffuse + t.specular;

    lit *= matAO;
    
#ifdef BINDLESS
    vec3 emissive = texture(globalTextures[nonuniformEXT(mat.emissiveTexIdx)], fragTexCoord).rgb * matEmissive.rgb;
#else
    vec3 emissive = texture(texEmissive, fragTexCoord).rgb * material.emissive.rgb;
#endif

    outColor = vec4(lit + emissive, albedoSample.a * baseColor.a);
}
