// web_compat.glsl — pont entre le modèle de shading desktop (Vulkan) et web (WGSL)
// depuis une source GLSL unique. Étape 16.2.
//
// Deux différences irréductibles empêchent la transpilation SPIR-V → WGSL de nos
// shaders desktop tels quels (ni naga ni Tint ne les franchissent) :
//   1. WGSL n'a pas de *combined image sampler* : il veut une texture et un
//      sampler séparés (`texture_2d` + `sampler`).
//   2. WGSL n'a pas de *push constant* : il faut un uniform buffer.
//
// Cette couche masque les deux derrière des macros. Côté desktop (chemin `#else`,
// sans -DWEB) elles s'étendent **exactement** en le code d'origine, donc le
// SPIR-V desktop est inchangé au bit près. Sous -DWEB elles produisent les formes
// compatibles WebGPU.
//
// Modèle d'usage — une texture se déclare puis se lit en enrobant son nom :
//
//     DECL_TEX2D(set, texBind, smpBind, albedo);   // déclaration
//     vec4 c = texture(TEX2D(albedo), uv);         // échantillonnage
//
// `TEX2D(albedo)` est le sampler combiné côté desktop, et `sampler2D(albedo_t,
// albedo_s)` côté web. Tous les builtins (texture/textureLod/textureSize/
// textureQueryLevels/…) acceptent cette expression, donc les sites d'appel sont
// identiques dans les deux modes.
//
// Bindings web : la texture garde son binding desktop ; le sampler prend un
// binding distinct explicite (`smpBind`) — pas d'arithmétique cachée, chaque
// paire est lisible et le layout WebGPU (Étape 16.4) s'aligne dessus 1:1.

#ifndef WEB_COMPAT_GLSL
#define WEB_COMPAT_GLSL

// Note : le paramètre de set est nommé `_set` et non `set` — sinon la
// substitution de macro remplacerait aussi le mot-clé `set` de `layout(set=…)`.

#ifdef WEB
// ── WebGPU / WGSL : texture + sampler séparés, push constant → UBO ───────────

// L'analyse d'uniformité WGSL (Tint) interdit tout échantillonnage à dérivées
// implicites (texture/textureSampleCompare) hors du flux de contrôle uniforme —
// or nos ombres sont lues dans des boucles de lumières avec early-return par
// fragment. La variante LOD explicite (textureSampleCompareLevel) est autorisée
// partout et identique pour une shadow map sans mips.
#extension GL_EXT_texture_shadow_lod : require

#define DECL_TEX2D(_set, tb, sb, name) \
    layout(set = _set, binding = tb) uniform texture2D name##_t; \
    layout(set = _set, binding = sb) uniform sampler   name##_s
#define TEX2D(name) sampler2D(name##_t, name##_s)

#define DECL_TEX3D(_set, tb, sb, name) \
    layout(set = _set, binding = tb) uniform texture3D name##_t; \
    layout(set = _set, binding = sb) uniform sampler   name##_s
#define TEX3D(name) sampler3D(name##_t, name##_s)

#define DECL_TEXCUBE(_set, tb, sb, name) \
    layout(set = _set, binding = tb) uniform textureCube name##_t; \
    layout(set = _set, binding = sb) uniform sampler     name##_s
#define TEXCUBE(name) samplerCube(name##_t, name##_s)

// Ombres : texture 2D-array + sampler de comparaison (WGSL: texture_depth_2d_array
// + sampler_comparison, échantillonné par textureSampleCompare).
#define DECL_SHADOW2DARRAY(_set, tb, sb, name) \
    layout(set = _set, binding = tb) uniform texture2DArray name##_t; \
    layout(set = _set, binding = sb) uniform samplerShadow  name##_s
#define SHADOW2DARRAY(name) sampler2DArrayShadow(name##_t, name##_s)
#define SAMPLE_SHADOW2DARRAY(name, coord) \
    textureLod(SHADOW2DARRAY(name), coord, 0.0)

// Le bloc par-objet (model + params) devient un UBO. Set 3 est libre côté web
// (pas de bindless). L'accès `push.field` reste identique.
#define PUSH_QUALIFIER layout(set = 3, binding = 0) uniform

#else
// ── Desktop / Vulkan : combined image samplers + push constants (inchangé) ───

#define DECL_TEX2D(_set, tb, sb, name)         layout(set = _set, binding = tb) uniform sampler2D name
#define TEX2D(name) name
#define DECL_TEX3D(_set, tb, sb, name)         layout(set = _set, binding = tb) uniform sampler3D name
#define TEX3D(name) name
#define DECL_TEXCUBE(_set, tb, sb, name)       layout(set = _set, binding = tb) uniform samplerCube name
#define TEXCUBE(name) name
#define DECL_SHADOW2DARRAY(_set, tb, sb, name) layout(set = _set, binding = tb) uniform sampler2DArrayShadow name
#define SHADOW2DARRAY(name) name
#define SAMPLE_SHADOW2DARRAY(name, coord) texture(name, coord)
#define PUSH_QUALIFIER layout(push_constant) uniform

#endif

#endif  // WEB_COMPAT_GLSL
