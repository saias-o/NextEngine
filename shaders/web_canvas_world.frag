#version 450
#extension GL_EXT_nonuniform_qualifier : require

layout(set = 0, binding = 0) uniform sampler2D globalTextures[8192];

layout(location = 0) in vec2 fragUV;
layout(location = 1) flat in uint fragTextureId;
layout(location = 2) in float fragAlpha;

layout(location = 0) out vec4 outColor;

void main() {
    vec4 color = texture(globalTextures[nonuniformEXT(fragTextureId)], fragUV);
    color.a *= fragAlpha;
    if (color.a <= 0.001) discard;
    outColor = color;
}
