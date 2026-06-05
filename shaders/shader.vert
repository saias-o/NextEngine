#version 450

layout(set = 0, binding = 0) uniform CameraUBO {
    mat4 view;
    mat4 proj;
} cam;

layout(push_constant) uniform PushConstants {
    mat4 model;
    vec4 params;  // x = useLightmap (read in the fragment stage)
} push;

#ifdef BINDLESS
struct InstanceData {
    mat4 model;
    vec4 boundingSphere;
    uint materialIndex;
    uint pad1, pad2, pad3;
};

layout(std140, set = 3, binding = 0) readonly buffer InstanceBuffer {
    InstanceData instances[];
};
#endif

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec3 inColor;
layout(location = 3) in vec2 inTexCoord;
layout(location = 4) in vec2 inLightmapUV;
layout(location = 5) in vec4 inTangent;

layout(location = 0) out vec3 fragWorldPos;
layout(location = 1) out vec3 fragNormal;
layout(location = 2) out vec3 fragColor;
layout(location = 3) out vec2 fragTexCoord;
layout(location = 4) out vec2 fragLightmapUV;
layout(location = 5) out vec3 fragTangent;
layout(location = 6) out vec3 fragBitangent;

#ifdef BINDLESS
layout(location = 7) flat out uint fragMaterialIndex;
#endif

void main() {
#ifdef BINDLESS
    mat4 modelMat = instances[gl_InstanceIndex].model;
    fragMaterialIndex = instances[gl_InstanceIndex].materialIndex;
#else
    mat4 modelMat = push.model;
#endif

    vec4 worldPos = modelMat * vec4(inPosition, 1.0);
    fragWorldPos = worldPos.xyz;
    // Normal matrix (handles non-uniform scale). Cheap enough per-vertex.
    mat3 normalMatrix = mat3(transpose(inverse(modelMat)));
    fragNormal = normalMatrix * inNormal;
    fragTangent = normalize(normalMatrix * inTangent.xyz);
    fragBitangent = cross(fragNormal, fragTangent) * inTangent.w;
    fragColor = inColor;
    fragTexCoord = inTexCoord;
    fragLightmapUV = inLightmapUV;
    gl_Position = cam.proj * cam.view * worldPos;
}
