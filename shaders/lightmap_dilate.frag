#version 450

// Seam dilation: the baked lightmap stores coverage in alpha (1 = a triangle was
// rasterized here, 0 = empty UV gutter). For each empty texel, search outward in
// rings for the nearest covered texel and copy its color. This pads island edges
// so bilinear sampling in the scene pass doesn't bleed black seams.

layout(set = 0, binding = 0) uniform sampler2D src;

layout(push_constant) uniform Push {
    vec2 texel;   // 1 / lightmap size
    int radius;   // max gutter width searched, in texels
} p;

layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 outColor;

void main() {
    vec4 c = texture(src, uv);
    if (c.a >= 0.5) { outColor = c; return; }  // already covered

    for (int r = 1; r <= p.radius; ++r) {
        for (int dy = -r; dy <= r; ++dy)
        for (int dx = -r; dx <= r; ++dx) {
            if (max(abs(dx), abs(dy)) != r) continue;  // current ring only
            vec4 s = texture(src, uv + vec2(float(dx), float(dy)) * p.texel);
            if (s.a >= 0.5) { outColor = vec4(s.rgb, 1.0); return; }
        }
    }
    outColor = vec4(0.0);  // beyond the dilation radius
}
