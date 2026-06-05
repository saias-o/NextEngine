#version 450

// HDR → LDR tonemap pass.
// Reads the floating-point scene color, applies exposure control, maps through
// the ACES filmic curve (which gracefully compresses highlights), and finishes
// with a linear-to-sRGB gamma transfer. Output goes to the swapchain (8-bit).

layout(set = 0, binding = 0) uniform sampler2D hdrInput;

layout(push_constant) uniform PushConstants {
    float exposure;   // multiplier applied before tonemapping (1.0 = neutral)
} push;

layout(location = 0) in vec2 fragUV;
layout(location = 0) out vec4 outColor;

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

void main() {
    vec3 hdr = texture(hdrInput, fragUV).rgb;

    // Exposure: a simple linear scale before the curve.
    hdr *= push.exposure;

    // Tonemap: compress the HDR range into displayable [0,1].
    vec3 mapped = acesFilmic(hdr);

    // Gamma: convert from linear light to sRGB for the monitor.
    // Using the simple pow approximation (close enough for games; the exact
    // piecewise sRGB transfer adds negligible quality at higher cost).
    vec3 srgb = pow(mapped, vec3(1.0 / 2.2));

    outColor = vec4(srgb, 1.0);
}
