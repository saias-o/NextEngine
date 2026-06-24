#version 450

// Animated water surface — fragment stage. Modern, mobile/VR-friendly and texture-
// free. The fine ripple normals come from FBM value-noise with ANALYTIC derivatives
// (so the normal is read straight from the gradient — no finite differences), in
// TWO layers at different scale/speed/direction (the procedural equivalent of two
// scrolling normal maps). Each FBM octave is rotated with a non-aligned matrix and a
// non-integer lacunarity, and the sample domain is warped, so there is no visible
// tiling on large expanses. Ripples fade with distance to kill shimmer. Shading is
// Fresnel reflection (environment map if present, else a cheap procedural sky), a
// sun sparkle, a tinted refracted body, and crest foam — every knob parameterized.

#include "lighting.glsl"

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

layout(location = 0) in vec3 fragWorldPos;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in float fragCrest;

layout(location = 0) out vec4 outColor;

const mat2 ROT = mat2(0.80, -0.60, 0.60, 0.80);  // non-axis-aligned octave rotation

float hash21(vec2 p) {
    p = fract(p * vec2(127.1, 311.7));
    p += dot(p, p + 34.23);
    return fract(p.x * p.y);
}

// Value noise with analytic derivative: returns (value, d/dx, d/dy). No filtering
// taps — the gradient is exact, which is exactly what a normal needs.
vec3 noised(vec2 x) {
    vec2 p = floor(x);
    vec2 f = fract(x);
    vec2 u = f * f * (3.0 - 2.0 * f);
    vec2 du = 6.0 * f * (1.0 - f);
    float a = hash21(p + vec2(0.0, 0.0));
    float b = hash21(p + vec2(1.0, 0.0));
    float c = hash21(p + vec2(0.0, 1.0));
    float d = hash21(p + vec2(1.0, 1.0));
    float k1 = b - a;
    float k2 = c - a;
    float k3 = a - b - c + d;
    float v = a + k1 * u.x + k2 * u.y + k3 * u.x * u.y;
    vec2 deriv = du * vec2(k1 + k3 * u.y, k2 + k3 * u.x);
    return vec3(v, deriv);
}

// Accumulated gradient (xz) of an FBM height field, scrolled by `flow`. Rotating the
// domain each octave + non-integer lacunarity removes axis-aligned repetition.
vec2 fbmGrad(vec2 p, vec2 flow, float t) {
    vec2 grad = vec2(0.0);
    float amp = 1.0, freq = 1.0, norm = 0.0;
    for (int i = 0; i < 4; ++i) {
        vec3 n = noised(p * freq + flow * t * freq);
        grad += amp * freq * n.yz;
        norm += amp;
        amp *= 0.5;
        freq *= 1.93;
        p = ROT * p;
        flow = ROT * flow;
    }
    return grad / max(norm, 1e-3);
}

vec2 dirFromAngle(float deg) {
    float a = radians(deg);
    return vec2(cos(a), sin(a));
}

// Cheap analytic sky for reflections when no environment map is bound.
vec3 proceduralSky(vec3 dir, vec3 sunDir, vec3 sunCol) {
    float up = clamp(dir.y * 0.5 + 0.5, 0.0, 1.0);
    vec3 horizon = vec3(0.72, 0.82, 0.93);
    vec3 zenith  = vec3(0.17, 0.40, 0.78);
    vec3 sky = mix(horizon, zenith, pow(up, 0.55));
    float d = max(dot(dir, -sunDir), 0.0);
    sky += sunCol * pow(d, 800.0) * 3.0;  // sun disc
    sky += sunCol * pow(d, 8.0) * 0.12;   // soft glow
    return sky;
}

void main() {
    vec3 camPos = lights.cameraPos.xyz;
    vec3 V = normalize(camPos - fragWorldPos);

    // Distance fade: calmer ripples far away (anti-shimmer + hides any repetition).
    float dist = length(camPos - fragWorldPos);
    float fade = clamp(1.0 - dist / max(push.misc.z, 1.0), 0.15, 1.0);

    // Low-frequency domain warp breaks up large-scale regularity before sampling.
    vec2 wp = fragWorldPos.xz;
    vec2 warp = push.misc.y * fbmGrad(wp * 0.03, vec2(0.05, 0.03), push.misc.x * 0.2);
    vec2 wpW = wp + warp;

    // Two procedural normal layers (different scale / speed / direction), blended.
    vec2 g1 = fbmGrad(wpW * push.detail1.x, dirFromAngle(push.detail1.w) * push.detail1.y, push.misc.x) * push.detail1.z;
    vec2 g2 = fbmGrad(wpW * push.detail2.x, dirFromAngle(push.detail2.w) * push.detail2.y, push.misc.x) * push.detail2.z;
    vec2 grad = (g1 + g2) * fade;

    vec3 N = normalize(fragNormal + vec3(-grad.x, 0.0, -grad.y));
    float NdotV = max(dot(N, V), 1e-3);

    // Primary directional light = the sun (for the sparkle / sky disc).
    vec3 sunDir = vec3(0.0, -1.0, 0.0);
    vec3 sunCol = vec3(1.0);
    for (int i = 0; i < lights.counts.x; ++i) {
        if (int(lights.lights[i].dirType.w + 0.5) == 0) {
            sunDir = normalize(lights.lights[i].dirType.xyz);
            sunCol = lights.lights[i].colorInt.rgb * lights.lights[i].colorInt.w;
            break;
        }
    }

    bool hasEnv = lights.environmentParams.x > 0.5;
    float rough = clamp(push.deep.w, 0.02, 0.4);

    // Refracted body colour: deep tint lifted by ambient / sky irradiance.
    vec3 ambient = hasEnv
        ? sampleEnvironmentLod(vec3(0.0, 1.0, 0.0), environmentMaxLod()) * lights.environmentParams.y
        : lights.ambient.rgb;
    vec3 body = push.deep.rgb * (0.25 + 0.75 * ambient);

    // Reflection: real environment if available, else the procedural sky.
    vec3 R = reflect(-V, N);
    R.y = max(R.y, 0.02);
    vec3 refl = hasEnv
        ? sampleEnvironmentLod(R, rough * environmentMaxLod()) * lights.environmentParams.z
        : proceduralSky(R, sunDir, sunCol);

    // Fresnel blends body -> reflection toward grazing angles.
    float F = 0.02 + 0.98 * pow(1.0 - NdotV, push.look.x);
    vec3 color = mix(body, refl, clamp(F * push.shallow.w, 0.0, 1.0));

    // Sun sparkle (Blinn-Phong).
    vec3 H = normalize(-sunDir + V);
    color += sunCol * pow(max(dot(N, H), 0.0), push.look.y) * push.look.z;

    // Foam on wave crests.
    float foam = smoothstep(push.look.w, 1.0, fragCrest) * push.misc.w;
    color = mix(color, push.shallow.rgb, clamp(foam, 0.0, 1.0));

    outColor = vec4(color, 1.0);
}
