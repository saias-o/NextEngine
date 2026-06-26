#version 450

layout(location = 0) in vec4 fragColor;
layout(location = 1) in vec2 fragUV;

layout(location = 0) out vec4 outColor;

void main() {
    vec2 d = fragUV * 2.0 - 1.0;
    float r2 = dot(d, d);
    if (r2 > 1.0) discard;

    float feather = smoothstep(1.0, 0.65, r2);
    outColor = vec4(fragColor.rgb, fragColor.a * feather);
}
