#version 450

// Depth-only shadow pass. mvp = lightViewProj * model.
layout(push_constant) uniform PushConstants {
    mat4 mvp;
} push;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec3 inColor;
layout(location = 3) in vec2 inTexCoord;

void main() {
    gl_Position = push.mvp * vec4(inPosition, 1.0);
}
