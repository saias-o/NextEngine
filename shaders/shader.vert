#version 450

layout(binding = 0) uniform CameraUBO {
    mat4 view;
    mat4 proj;
} cam;

layout(push_constant) uniform PushConstants {
    mat4 model;
} push;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec3 inColor;
layout(location = 3) in vec2 inTexCoord;

layout(location = 0) out vec3 fragWorldPos;
layout(location = 1) out vec3 fragNormal;
layout(location = 2) out vec3 fragColor;
layout(location = 3) out vec2 fragTexCoord;

void main() {
    vec4 worldPos = push.model * vec4(inPosition, 1.0);
    fragWorldPos = worldPos.xyz;
    // Normal matrix (handles non-uniform scale). Cheap enough per-vertex.
    fragNormal = mat3(transpose(inverse(push.model))) * inNormal;
    fragColor = inColor;
    fragTexCoord = inTexCoord;
    gl_Position = cam.proj * cam.view * worldPos;
}
