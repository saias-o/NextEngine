#version 450

// Animated water surface — vertex stage. Generates a tessellated grid procedurally
// (no vertex buffer, like the skybox triangle) and displaces it with several
// octaves of directional waves. Each octave is ROTATED and uses a NON-harmonic
// frequency ratio, so the swell never reads as a repeating grid. Optional Gerstner-
// like horizontal "choppiness" sharpens the crests. Produces the position, an
// analytic normal, and a crest factor (for foam) for the fragment stage.
//
// When the WaterNode has a shore (beach/lake), the wave displacement is FADED OUT in
// shallow water so the surface meets the sand cleanly at the waterline (depth 0) and
// big swells never poke through the beach — the shore seam relies on this.
//
// Per-eye camera matrices come from the shared global camera UBO (set 0, binding
// 0); the MULTIVIEW variant indexes them by gl_ViewIndex, so the same water works
// for desktop (mono) and XR (stereo) through the engine's one pipeline.

#ifdef MULTIVIEW
#extension GL_EXT_multiview : require
#endif

layout(set = 0, binding = 0) uniform CameraUBO {
    mat4 view[2];
    mat4 proj[2];
} cam;

#include "water_common.glsl"

layout(location = 0) out vec3 fragWorldPos;
layout(location = 1) out vec3 fragNormal;
layout(location = 2) out float fragCrest;

const int RES = 128;  // grid resolution per side; must match kGridRes in the renderer

void main() {
    GpuWater w = waters.items[push.index];

    // Procedural grid: 6 vertices per quad, RES*RES quads, two triangles each.
    int quad = gl_VertexIndex / 6;
    int corner = gl_VertexIndex % 6;
    int qx = quad % RES;
    int qz = quad / RES;
    const vec2 OFF[6] = vec2[6](
        vec2(0, 0), vec2(1, 0), vec2(1, 1),
        vec2(0, 0), vec2(1, 1), vec2(0, 1));
    vec2 cell = (vec2(float(qx), float(qz)) + OFF[corner]) / float(RES);  // [0,1]
    vec2 xz = w.area.xz + (cell - 0.5) * 2.0 * w.area.w;

    float t = push.time;
    float wavelength = max(w.waveA.y, 0.01);
    float speed = w.waveA.z;
    float chop = w.waveA.w;

    // Waves flatten as the bottom rises: 1 in open/deep water, 0 at (and past) the
    // waterline. Keeps crests from clipping the beach and lands the surface exactly
    // on the sand so the fragment alpha edge lines up.
    float depth = waterDepthAt(xz, w);
    float shallow = (int(w.shoreMode.x + 0.5) == 0)
        ? 1.0
        : smoothstep(0.0, max(w.shoreTune.w, 0.01), depth);

    // Multi-octave swell, AIMED AT THE SHORE. The base heading is the shoreward wave
    // direction (waveDirAt); octaves fan out only slightly around it so the crests
    // stay parallel to the shore and roll straight in, while a non-harmonic frequency
    // growth still kills the grid look. With no shore, octaves keep the original wide
    // spread (~37°/octave) so endless ocean water is unchanged.
    int waveMode = int(w.shoreMode.x + 0.5);
    vec2 baseDir = waveDirAt(xz, w);
    const float FAN[5] = float[5](0.0, 13.0, -10.0, 18.0, -6.0);  // shoreward spread (deg)
    float h = 0.0, dhdx = 0.0, dhdz = 0.0;
    vec2 disp = vec2(0.0);            // horizontal Gerstner-like pull (choppiness)
    float a = w.waveA.x;
    float wn = 6.2831853 / wavelength;
    for (int i = 0; i < 5; ++i) {
        float offDeg = (waveMode == 0) ? 36.87 * float(i) : FAN[i];
        vec2 dir = rotate2(baseDir, radians(offDeg));
        float ang = dot(dir, xz) * wn + t * speed * wn;
        float s = sin(ang), c = cos(ang);
        h    += a * s;
        dhdx += a * wn * dir.x * c;
        dhdz += a * wn * dir.y * c;
        disp -= dir * (a * chop * c);   // pull vertices toward crests
        a *= 0.62;                       // amplitude falloff
        wn *= 1.87;                      // non-harmonic frequency growth
    }

    // Fade the whole displacement toward the shore.
    h *= shallow; dhdx *= shallow; dhdz *= shallow; disp *= shallow;

    vec2 xzC = xz + disp;
    vec3 pos = vec3(xzC.x, w.area.y + h, xzC.y);
    fragWorldPos = pos;
    fragNormal = normalize(vec3(-dhdx, 1.0, -dhdz));
    fragCrest = clamp(h / max(w.waveA.x, 0.001) * 0.5 + 0.5, 0.0, 1.0);

#ifdef MULTIVIEW
    int vi = gl_ViewIndex;
#else
    int vi = 0;
#endif
    gl_Position = cam.proj[vi] * cam.view[vi] * vec4(pos, 1.0);
}
