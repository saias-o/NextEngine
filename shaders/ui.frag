#version 450
#extension GL_EXT_nonuniform_qualifier : require

layout(location = 0) in vec2 fragUV;
layout(location = 1) in vec4 fragColor;
layout(location = 2) flat in uint fragTextureId;
layout(location = 3) flat in uint fragHasTexture;

layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D bindlessTextures[];

void main() {
    vec4 texColor = vec4(1.0);
    
    if (fragHasTexture != 0) {
        texColor = texture(bindlessTextures[nonuniformEXT(fragTextureId)], fragUV);
    }
    
    outColor = fragColor * texColor;
}
