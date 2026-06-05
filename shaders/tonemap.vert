#version 450

// Fullscreen triangle: three vertices cover the entire viewport without a vertex
// buffer. The oversized triangle is clipped by the rasterizer. This is cheaper
// than a quad (no diagonal edge = no redundant fragment work at the seam).
//
//   v2 (−1,3)
//    |\
//    | \
//    |  \
//    |---+--- v1 (3,−1)
//    |   |  /
//    |   | /
//    +---+/
//   v0 (−1,−1)
//
// Vulkan's NDC has Y pointing downward, so the UVs are flipped:
//   v0 → UV(0,1), v1 → UV(2,1), v2 → UV(0,−1)
// The rasterizer interpolates across the visible [0,1]² region automatically.

layout(location = 0) out vec2 fragUV;

void main() {
    vec2 pos;
    if (gl_VertexIndex == 0) pos = vec2(0.0, 0.0);
    else if (gl_VertexIndex == 1) pos = vec2(0.0, 2.0);
    else pos = vec2(2.0, 0.0);

    // Map to clip space [-1,1] and compute matching UV.
    // Vulkan Y-flip: NDC Y goes top-to-bottom, UV Y should go 0 (top) to 1 (bottom).
    gl_Position = vec4(pos * 2.0 - 1.0, 0.0, 1.0);
    fragUV = vec2(pos.x, pos.y);
}
