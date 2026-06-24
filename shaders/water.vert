#version 450

// Animated water surface — vertex stage. Generates a tessellated grid procedurally
// (no vertex buffer, like the skybox triangle) and displaces it with several
// octaves of directional waves. Each octave is ROTATED and uses a NON-harmonic
// frequency ratio, so the swell never reads as a repeating grid. Optional Gerstner-
// like horizontal "choppiness" sharpens the crests. Produces the position, an
// analytic normal, and a crest factor (for foam) for the fragment stage.
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

layout(push_constant) uniform WaterPush {
    vec4 area;     // x=centerX, y=height, z=centerZ, w=halfSize
    vec4 deep;     // rgb water color, w=roughness
    vec4 shallow;  // rgb foam color, w=reflectivity
    vec4 waveA;    // x=amplitude, y=wavelength, z=speed, w=choppiness
    vec4 detail1;  // x=scale, y=speed, z=strength, w=angleDeg
    vec4 detail2;  // x=scale, y=speed, z=strength, w=angleDeg
    vec4 look;     // x=fresnelPower, y=specularPower, z=specularIntensity, w=foamThreshold
    vec4 misc;     // x=time, y=warpAmount, z=detailFadeDistance, w=foamIntensity
} push;

layout(location = 0) out vec3 fragWorldPos;
layout(location = 1) out vec3 fragNormal;
layout(location = 2) out float fragCrest;

const int RES = 128;  // grid resolution per side; must match kGridRes in the renderer

void main() {
    // Procedural grid: 6 vertices per quad, RES*RES quads, two triangles each.
    int quad = gl_VertexIndex / 6;
    int corner = gl_VertexIndex % 6;
    int qx = quad % RES;
    int qz = quad / RES;
    const vec2 OFF[6] = vec2[6](
        vec2(0, 0), vec2(1, 0), vec2(1, 1),
        vec2(0, 0), vec2(1, 1), vec2(0, 1));
    vec2 cell = (vec2(float(qx), float(qz)) + OFF[corner]) / float(RES);  // [0,1]
    vec2 xz = push.area.xz + (cell - 0.5) * 2.0 * push.area.w;

    float t = push.misc.x;
    float wavelength = max(push.waveA.y, 0.01);
    float speed = push.waveA.z;
    float chop = push.waveA.w;

    // Multi-octave swell. Rotating the direction by a non-aligned matrix and scaling
    // the frequency by a non-integer factor each octave kills the grid look.
    const mat2 ROT = mat2(0.80, -0.60, 0.60, 0.80);  // ~37°, not axis-aligned
    float h = 0.0, dhdx = 0.0, dhdz = 0.0;
    vec2 disp = vec2(0.0);            // horizontal Gerstner-like pull (choppiness)
    vec2 dir = normalize(vec2(0.9, 0.35));
    float a = push.waveA.x;
    float w = 6.2831853 / wavelength;
    for (int i = 0; i < 5; ++i) {
        float ang = dot(dir, xz) * w + t * speed * w;
        float s = sin(ang), c = cos(ang);
        h    += a * s;
        dhdx += a * w * dir.x * c;
        dhdz += a * w * dir.y * c;
        disp -= dir * (a * chop * c);   // pull vertices toward crests
        a *= 0.62;                       // amplitude falloff
        w *= 1.87;                       // non-harmonic frequency growth
        dir = ROT * dir;                 // rotate so octaves don't align
    }

    vec2 xzC = xz + disp;
    vec3 pos = vec3(xzC.x, push.area.y + h, xzC.y);
    fragWorldPos = pos;
    fragNormal = normalize(vec3(-dhdx, 1.0, -dhdz));
    fragCrest = clamp(h / max(push.waveA.x, 0.001) * 0.5 + 0.5, 0.0, 1.0);

#ifdef MULTIVIEW
    int vi = gl_ViewIndex;
#else
    int vi = 0;
#endif
    gl_Position = cam.proj[vi] * cam.view[vi] * vec4(pos, 1.0);
}
