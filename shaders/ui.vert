#version 450

// Un simple quad généré sans VBO (triangle strip)
// Indices: 0, 1, 2, 3
// Positions: (0,0), (1,0), (0,1), (1,1)

layout(push_constant) uniform PushConstants {
    vec2 position;    // Position X,Y (Top-Left) en pixels
    vec2 size;        // Largeur, Hauteur en pixels
    vec2 screenSize;  // Largeur, Hauteur de l'écran en pixels
    uint textureId;   // Index bindless
    uint hasTexture;  // 1 si utilisation de texture, 0 sinon
    vec4 color;       // Couleur (RGBA)
} push;

layout(location = 0) out vec2 fragUV;
layout(location = 1) out vec4 fragColor;
layout(location = 2) flat out uint fragTextureId;
layout(location = 3) flat out uint fragHasTexture;

void main() {
    // Génération d'un quad (Triangle Strip) avec gl_VertexIndex
    vec2 quad[4] = vec2[](
        vec2(0.0, 0.0), // Top-Left
        vec2(1.0, 0.0), // Top-Right
        vec2(0.0, 1.0), // Bottom-Left
        vec2(1.0, 1.0)  // Bottom-Right
    );
    
    vec2 localPos = quad[gl_VertexIndex];
    
    // UVs
    fragUV = localPos;
    
    // Position pixel à l'écran
    vec2 pixelPos = push.position + localPos * push.size;
    
    // Projection Orthographique (0 -> screenSize) vers NDC (-1 -> 1)
    // X: 0 -> width devient -1 -> 1
    // Y: 0 -> height devient -1 -> 1 (Attention vulkan Y descend)
    vec2 ndc = (pixelPos / push.screenSize) * 2.0 - 1.0;
    
    gl_Position = vec4(ndc.x, ndc.y, 0.0, 1.0);
    
    fragColor = push.color;
    fragTextureId = push.textureId;
    fragHasTexture = push.hasTexture;
}
