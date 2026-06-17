#version 450

// Debug line drawing (skeleton bones, gizmos). Reuses the scene's set-0 camera
// UBO (view[2]/proj[2]; index 0 — debug lines are desktop/mono only) and the
// standard Vertex layout (only position + color are read).

layout(set = 0, binding = 0) uniform CameraUBO {
    mat4 view[2];
    mat4 proj[2];
} cam;

layout(location = 0) in vec3 inPosition;
layout(location = 2) in vec3 inColor;

layout(location = 0) out vec3 fragColor;

void main() {
    fragColor = inColor;
    gl_Position = cam.proj[0] * cam.view[0] * vec4(inPosition, 1.0);
}
