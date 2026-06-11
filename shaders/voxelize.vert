#version 450

// Scene voxelization (P1) — feeds the DDGI ray-march (P2). The scene is rendered
// three times, once per dominant axis, into an attachment-less pass; each fragment
// writes the surface albedo into a 3D voxel grid (voxelize.frag). The exact raster
// orientation is irrelevant because the voxel coordinate is derived from the world
// position, not gl_FragCoord — we only need every triangle to be rasterized densely
// from at least one of the three axes.

layout(set = 0, binding = 1) uniform VoxelUBO {
    vec4 origin;    // xyz = volume min corner (world)
    vec4 extent;    // xyz = volume size (world units)
    ivec4 res;      // xyz = voxel resolution
    mat4 axisVP[3]; // orthographic view-proj per dominant axis
} vox;

layout(push_constant) uniform Push {
    mat4 model;
    uint axis;
} push;

layout(location = 0) in vec3 inPosition;
layout(location = 2) in vec3 inColor;
layout(location = 3) in vec2 inTexCoord;

layout(location = 0) out vec3 vWorldPos;
layout(location = 1) out vec3 vColor;
layout(location = 2) out vec2 vUV;

void main() {
    vec4 wp = push.model * vec4(inPosition, 1.0);
    vWorldPos = wp.xyz;
    vColor = inColor;
    vUV = inTexCoord;
    gl_Position = vox.axisVP[push.axis] * wp;
}
