#version 450

layout(push_constant) uniform PushConstants {
    mat4 model;
    vec4 color;
    vec4 params;
    vec4 localCenter;
    vec4 localHalfExtent;
} push;

layout(location = 0) out vec4 outColor;

void main() {
    outColor = push.color;
}
