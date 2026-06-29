#version 450

layout(set = 0, binding = 0) uniform sampler2D sourceInput;

layout(push_constant) uniform PushConstants {
    vec4 sourceRect;
    vec4 params; // z radius
} push;

layout(location = 0) in vec2 fragUV;
layout(location = 0) out vec4 outColor;

void main() {
    vec2 texel = 1.0 / vec2(textureSize(sourceInput, 0));
    float radius = clamp(push.params.z, 0.5, 8.0) * 0.35;
    vec2 r = texel * radius;

    vec3 color = texture(sourceInput, fragUV).rgb * 0.36;
    color += texture(sourceInput, fragUV + r * vec2( 1.0,  0.0)).rgb * 0.12;
    color += texture(sourceInput, fragUV + r * vec2(-1.0,  0.0)).rgb * 0.12;
    color += texture(sourceInput, fragUV + r * vec2( 0.0,  1.0)).rgb * 0.12;
    color += texture(sourceInput, fragUV + r * vec2( 0.0, -1.0)).rgb * 0.12;
    color += texture(sourceInput, fragUV + r * vec2( 1.0,  1.0)).rgb * 0.04;
    color += texture(sourceInput, fragUV + r * vec2(-1.0,  1.0)).rgb * 0.04;
    color += texture(sourceInput, fragUV + r * vec2( 1.0, -1.0)).rgb * 0.04;
    color += texture(sourceInput, fragUV + r * vec2(-1.0, -1.0)).rgb * 0.04;

    outColor = vec4(color, 1.0);
}
