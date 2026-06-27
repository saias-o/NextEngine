#version 450

// HDR → LDR tonemap pass.
// Reads the floating-point scene color, applies exposure control, maps through
// the ACES filmic curve (which gracefully compresses highlights), and finishes
// with a linear-to-sRGB gamma transfer. Output goes to the swapchain (8-bit).

layout(set = 0, binding = 0) uniform sampler2D hdrInput;
layout(set = 0, binding = 1) uniform sampler2D depthInput;

layout(push_constant) uniform PushConstants {
    mat4 invProjection;
    vec4 aoParams;        // x enabled, y radius, z intensity, w power
    vec4 fogColor;        // rgb = linear HDR fog color
    vec4 fogParams;       // x enabled, y start, z density, w exposure
    vec4 bloomParams;     // x enabled, y threshold, z intensity, w radius px
    vec4 sourceRect;      // xy = source UV origin, zw = source UV size
} push;

layout(location = 0) in vec2 fragUV;
layout(location = 0) out vec4 outColor;

const int AO_DIR_COUNT = 8;
const int AO_STEP_COUNT = 2;
const float AO_BIAS = 0.03;
const float AO_NORMAL_EPSILON = 1e-4;
const int BLOOM_SAMPLE_COUNT = 12;
const float BLOOM_SOFT_KNEE = 0.35;

vec2 sourceUV(vec2 viewportUV) {
    return push.sourceRect.xy + clamp(viewportUV, vec2(0.0), vec2(1.0)) * push.sourceRect.zw;
}

vec4 sampleHdr(vec2 viewportUV) {
    return texture(hdrInput, sourceUV(viewportUV));
}

float sampleDepth(vec2 viewportUV) {
    return texture(depthInput, sourceUV(viewportUV)).r;
}

vec2 viewportTexel() {
    vec2 sourcePixels = vec2(textureSize(hdrInput, 0)) * max(push.sourceRect.zw, vec2(1e-6));
    return 1.0 / max(sourcePixels, vec2(1.0));
}

// ACES filmic tonemap — Krzysztof Narkowicz's 2015 fit.
// Maps [0,∞) HDR values to [0,1] with a pleasant S-curve that keeps blacks
// dark and gracefully rolls off highlights without hard clipping.
vec3 acesFilmic(vec3 x) {
    const float a = 2.51;
    const float b = 0.03;
    const float c = 2.43;
    const float d = 0.59;
    const float e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

vec3 reconstructViewPosition(vec2 uv, float depth) {
    vec4 clip = vec4(uv * 2.0 - 1.0, depth, 1.0);
    vec4 view = push.invProjection * clip;
    return view.xyz / max(view.w, 1e-6);
}

vec3 viewNormalFromDepth(vec2 uv, vec3 center) {
    vec2 texel = viewportTexel();
    float depthX = sampleDepth(uv + vec2(texel.x, 0.0));
    float depthY = sampleDepth(uv + vec2(0.0, texel.y));
    vec3 px = reconstructViewPosition(uv + vec2(texel.x, 0.0), depthX);
    vec3 py = reconstructViewPosition(uv + vec2(0.0, texel.y), depthY);
    vec3 n = normalize(cross(py - center, px - center));
    vec3 viewDir = normalize(-center);
    if (dot(n, viewDir) < 0.0) n = -n;
    if (length(n) < AO_NORMAL_EPSILON) n = vec3(0.0, 0.0, 1.0);
    return n;
}

float ambientOcclusion(vec2 uv) {
    if (push.aoParams.x < 0.5) return 1.0;

    float centerDepth = sampleDepth(uv);
    if (centerDepth >= 0.9999) return 1.0;

    vec3 center = reconstructViewPosition(uv, centerDepth);
    vec3 normal = viewNormalFromDepth(uv, center);
    float viewDepth = max(abs(center.z), 0.05);
    vec2 uvRadius = vec2(0.5 * push.aoParams.y / viewDepth);

    float occlusion = 0.0;
    float samples = 0.0;
    for (int dirIndex = 0; dirIndex < AO_DIR_COUNT; ++dirIndex) {
        float angle = (float(dirIndex) + 0.5) * 6.28318530718 / float(AO_DIR_COUNT);
        vec2 dir = vec2(cos(angle), sin(angle));

        for (int stepIndex = 1; stepIndex <= AO_STEP_COUNT; ++stepIndex) {
            float stepT = float(stepIndex) / float(AO_STEP_COUNT);
            vec2 sampleUV = uv + dir * uvRadius * stepT;
            if (any(lessThan(sampleUV, vec2(0.0))) || any(greaterThan(sampleUV, vec2(1.0))))
                continue;

            float depthSample = sampleDepth(sampleUV);
            if (depthSample >= 0.9999) continue;

            vec3 samplePos = reconstructViewPosition(sampleUV, depthSample);
            vec3 delta = samplePos - center;
            float dist = length(delta);
            if (dist <= AO_NORMAL_EPSILON || dist > push.aoParams.y) continue;

            float normalTerm = max(dot(normal, delta / dist) - AO_BIAS, 0.0);
            float rangeTerm = 1.0 - smoothstep(push.aoParams.y * 0.25, push.aoParams.y, dist);
            occlusion += normalTerm * rangeTerm;
            samples += 1.0;
        }
    }

    if (samples <= 0.0) return 1.0;
    float ao = 1.0 - push.aoParams.z * (occlusion / samples);
    return pow(clamp(ao, 0.0, 1.0), push.aoParams.w);
}

vec3 brightPass(vec3 color) {
    float threshold = push.bloomParams.y;
    float luminance = dot(color, vec3(0.2126, 0.7152, 0.0722));
    float knee = max(threshold * BLOOM_SOFT_KNEE, 0.001);
    float weight = smoothstep(threshold - knee, threshold + knee, luminance);
    return color * weight;
}

vec3 bloom(vec2 uv) {
    if (push.bloomParams.x < 0.5 || push.bloomParams.z <= 0.0 || push.bloomParams.w <= 0.0)
        return vec3(0.0);

    vec2 texel = viewportTexel();
    vec2 radius = texel * push.bloomParams.w;
    vec2 offsets[BLOOM_SAMPLE_COUNT] = vec2[](
        vec2( 1.0,  0.0), vec2(-1.0,  0.0), vec2( 0.0,  1.0), vec2( 0.0, -1.0),
        vec2( 0.707,  0.707), vec2(-0.707,  0.707), vec2( 0.707, -0.707), vec2(-0.707, -0.707),
        vec2( 2.0,  0.0), vec2(-2.0,  0.0), vec2( 0.0,  2.0), vec2( 0.0, -2.0)
    );

    vec3 sum = brightPass(sampleHdr(uv).rgb) * 0.18;
    float weightSum = 0.18;
    for (int i = 0; i < BLOOM_SAMPLE_COUNT; ++i) {
        float weight = i < 8 ? 0.075 : 0.055;
        sum += brightPass(sampleHdr(uv + offsets[i] * radius).rgb) * weight;
        weightSum += weight;
    }
    return (sum / weightSum) * push.bloomParams.z;
}

vec3 applyFog(vec3 color, vec2 uv) {
    if (push.fogParams.x < 0.5) return color;

    float depth = sampleDepth(uv);
    if (depth >= 0.9999) return color;

    vec3 viewPos = reconstructViewPosition(uv, depth);
    float distanceFog = max(length(viewPos) - push.fogParams.y, 0.0);
    float fogAmount = 1.0 - exp(-distanceFog * push.fogParams.z);
    return mix(color, push.fogColor.rgb, clamp(fogAmount, 0.0, 1.0));
}

void main() {
    vec4 hdr4 = sampleHdr(fragUV);
    vec3 hdr = hdr4.rgb;

    hdr *= ambientOcclusion(fragUV);
    hdr = applyFog(hdr, fragUV);
    hdr += bloom(fragUV);

    // Exposure: a simple linear scale before the curve.
    hdr *= push.fogParams.w;

    // Tonemap: compress the HDR range into displayable [0,1].
    vec3 mapped = acesFilmic(hdr);

    // Gamma: convert from linear light to sRGB for the monitor.
    // Using the simple pow approximation (close enough for games; the exact
    // piecewise sRGB transfer adds negligible quality at higher cost).
    vec3 srgb = pow(mapped, vec3(1.0 / 2.2));

    // Preserve scene coverage in alpha so XR passthrough composites correctly
    // (transparent where nothing was drawn). Ignored by the desktop swapchain.
    outColor = vec4(srgb, hdr4.a);
}
