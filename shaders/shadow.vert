#version 450

// Depth-only shadow pass: just transform the position into the light's clip
// space. mvp = lightViewProj * model (precomputed on the CPU). The other vertex
// attributes are part of the shared Vertex layout but unused here.
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
