#version 450
#extension GL_GOOGLE_include_directive : require
#ifndef WEB
#extension GL_EXT_nonuniform_qualifier : require
#endif

#include "web_compat.glsl"

layout(location = 0) in vec2 fragUV;
layout(location = 1) in vec4 fragColor;
layout(location = 2) flat in uint fragTextureId;
layout(location = 3) flat in uint fragHasTexture;

layout(location = 0) out vec4 outColor;

// Desktop uses a bindless texture array indexed per-draw; web (no bindless) binds
// a single UI texture per draw.
#ifdef WEB
DECL_TEX2D(0, 0, 1, uiTexture);
#else
layout(set = 0, binding = 0) uniform sampler2D bindlessTextures[];
#endif

void main() {
    vec4 texColor = vec4(1.0);

    if (fragHasTexture != 0) {
#ifdef WEB
        texColor = texture(TEX2D(uiTexture), fragUV);
#else
        texColor = texture(bindlessTextures[nonuniformEXT(fragTextureId)], fragUV);
#endif
    }

    outColor = fragColor * texColor;
}
