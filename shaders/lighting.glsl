// Shared PBR lighting math — the SINGLE source of truth for both the realtime
// scene shader (shader.frag) and the lightmap bake shader (bake.frag). Because
// both include this file, a bake evaluates exactly the same lighting as realtime.
//
// Uses Cook-Torrance microfacet BRDF with GGX distribution, Smith-Schlick
// geometry term, and Fresnel-Schlick approximation. The terms are split into a
// view-INdependent diffuse part (ambient + Lambertian + shadow → bakeable) and
// a view-DEPENDENT specular part (kept realtime, never baked).
// See diffuseIrradiance() / specularRadiance() / accumulate().

const float PI = 3.14159265359;
const int MAX_LIGHTS = 16;
const int MAX_SHADOWS = 4;

// One unified light covers directional / point / spot (mirrors GpuLight in C++).
struct Light {
    vec4 posRange;    // xyz world position (point/spot), w = range
    vec4 colorInt;    // rgb color, w = intensity
    vec4 dirType;     // xyz travel direction (dir/spot), w = type (0 dir,1 point,2 spot)
    vec4 spotShadow;  // x = cosInner, y = cosOuter, z = shadowIndex (-1 none), w unused
};

layout(set = 0, binding = 1) uniform LightingUBO {
    vec4 ambient;       // rgb = ambient color
    vec4 cameraPos;     // xyz = camera world position
    vec4 shadowParams;  // x = softness
    ivec4 counts;       // x = light count, y = mode (0 realtime, 1 baked)
    Light lights[MAX_LIGHTS];
    mat4 shadowMatrices[MAX_SHADOWS];
} lights;

layout(set = 0, binding = 2) uniform sampler2DArrayShadow shadowMap;

struct LightTerms { vec3 diffuse; vec3 specular; };

// ---------------------------------------------------------------------------
// Shadow
// ---------------------------------------------------------------------------

// Hardware-PCF shadow lookup. Returns the lit fraction in [0,1] (1 = fully lit).
float shadowFactor(int idx, vec3 worldPos, float ndotl) {
    vec4 clip = lights.shadowMatrices[idx] * vec4(worldPos, 1.0);
    vec3 proj = clip.xyz / clip.w;
    vec2 uv = proj.xy * 0.5 + 0.5;
    if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0 || proj.z > 1.0)
        return 1.0;
    float bias = clamp(0.0015 * tan(acos(clamp(ndotl, 0.0, 1.0))), 0.0, 0.004);
    float depthRef = proj.z - bias;
    vec2 texel = 1.0 / vec2(textureSize(shadowMap, 0).xy);
    float sum = 0.0;
    float softness = lights.shadowParams.x;
    for (int x = -1; x <= 1; ++x)
        for (int y = -1; y <= 1; ++y)
            sum += texture(shadowMap, vec4(uv + vec2(x, y) * texel * softness, float(idx), depthRef));
    return sum / 9.0;
}

// ---------------------------------------------------------------------------
// Cook-Torrance BRDF building blocks
// ---------------------------------------------------------------------------

// GGX / Trowbridge-Reitz Normal Distribution Function.
// Concentrates microfacet normals around the halfway vector h; alpha = roughness^2.
// D(h) = alpha^2 / (pi * ((n·h)^2 * (alpha^2 - 1) + 1)^2)
float distributionGGX(float NdotH, float roughness) {
    float a  = roughness * roughness;
    float a2 = a * a;
    float denom = NdotH * NdotH * (a2 - 1.0) + 1.0;
    return a2 / (PI * denom * denom);
}

// Smith-Schlick geometry sub-term G1 for a single direction.
// G1(n,x) = (n·x) / ((n·x)(1-k) + k)
// k = (roughness+1)^2 / 8  for direct (analytic) lighting.
float geometrySchlickG1(float NdotX, float k) {
    return NdotX / (NdotX * (1.0 - k) + k);
}

// Combined Smith geometry term: product of masking (view) and shadowing (light).
// G = G1(n,v) * G1(n,l)
float geometrySmith(float NdotV, float NdotL, float roughness) {
    float r = roughness + 1.0;
    float k = (r * r) / 8.0;        // k for direct lighting
    return geometrySchlickG1(NdotV, k) * geometrySchlickG1(NdotL, k);
}

// Fresnel-Schlick approximation.
// F = F0 + (1 - F0) * (1 - h·v)^5
// At grazing angles the surface reflects all light regardless of material.
vec3 fresnelSchlick(float HdotV, vec3 F0) {
    return F0 + (1.0 - F0) * pow(clamp(1.0 - HdotV, 0.0, 1.0), 5.0);
}

// ---------------------------------------------------------------------------
// Per-light contribution (Cook-Torrance GGX)
// ---------------------------------------------------------------------------

// Evaluates the full Cook-Torrance specular + Lambertian diffuse for one light,
// with attenuation, spot cone falloff, and shadow already folded in.
LightTerms lightContribution(int i, vec3 N, vec3 V, vec3 wp,
                              vec3 albedo, float metallic, float roughness) {
    // Clamp roughness to avoid NaN from division-by-zero in GGX when alpha→0.
    roughness = max(roughness, 0.04);

    Light lt = lights.lights[i];
    int type = int(lt.dirType.w + 0.5);
    vec3 radiance = lt.colorInt.rgb * lt.colorInt.w;

    // --- light vector & attenuation ---
    vec3 L;
    float atten = 1.0;
    if (type == 0) {
        L = normalize(-lt.dirType.xyz);              // directional
    } else {
        vec3 toLight = lt.posRange.xyz - wp;         // point / spot
        float dist = length(toLight);
        L = toLight / max(dist, 1e-4);
        float range = lt.posRange.w;
        atten = clamp(1.0 - (dist * dist) / (range * range), 0.0, 1.0);
        atten *= atten;
        if (type == 2) {                             // spot cone falloff
            float cosAngle = dot(-L, normalize(lt.dirType.xyz));
            atten *= smoothstep(lt.spotShadow.y, lt.spotShadow.x, cosAngle);
        }
    }

    // --- shadow ---
    float ndotl = dot(N, L);
    float shadow = 1.0;
    if (lt.spotShadow.z >= 0.0)
        shadow = shadowFactor(int(lt.spotShadow.z + 0.5), wp, ndotl);
    float vis = atten * shadow;

    // --- BRDF ---
    float NdotL = max(ndotl, 0.0);
    float NdotV = max(dot(N, V), 0.001);   // avoid zero for G denominator
    vec3 H = normalize(L + V);
    float NdotH = max(dot(N, H), 0.0);
    float HdotV = max(dot(H, V), 0.0);

    // Dielectrics reflect ~4% at normal incidence; metals use the albedo color.
    vec3 F0 = mix(vec3(0.04), albedo, metallic);

    // Cook-Torrance specular: D * G * F / (4 * NdotV * NdotL)
    float D = distributionGGX(NdotH, roughness);
    float G = geometrySmith(NdotV, NdotL, roughness);
    vec3  F = fresnelSchlick(HdotV, F0);

    // The denominator can approach zero at grazing angles; clamp to avoid inf.
    vec3 specBRDF = (D * G * F) / max(4.0 * NdotV * NdotL, 0.001);

    // Energy conservation: whatever isn't reflected is refracted (diffuse).
    // Metals have no diffuse because all refracted light is absorbed.
    vec3 kd = (vec3(1.0) - F) * (1.0 - metallic);

    // Lambertian diffuse: albedo / pi  (the pi in the numerator and the
    // NdotL in the rendering equation cancel the ones in the denominator).
    vec3 diffBRDF = kd * albedo / PI;

    LightTerms t;
    t.diffuse  = diffBRDF  * radiance * NdotL * vis;
    t.specular = specBRDF  * radiance * NdotL * vis;
    return t;
}

// ---------------------------------------------------------------------------
// Convenience aggregators
// ---------------------------------------------------------------------------

// Diffuse + ambient + shadows, view-independent → this is what gets baked.
// For the bake path a dummy V is fine since diffuse is view-independent (F is
// approximated with F0 at normal incidence, which is close enough for Lambertian).
vec3 diffuseIrradiance(vec3 N, vec3 wp, vec3 albedo, float metallic, float roughness) {
    vec3 sum = lights.ambient.rgb * albedo;
    // Use the surface normal as a stand-in view dir for the diffuse-only path.
    // The Fresnel term evaluated at NdotH≈1 yields ~F0, which is acceptable for
    // baking since specular highlights (which depend on the true V) are excluded.
    for (int i = 0; i < lights.counts.x; ++i)
        sum += lightContribution(i, N, N, wp, albedo, metallic, roughness).diffuse;
    return sum;
}

// View-dependent specular only → always evaluated live (never baked).
vec3 specularRadiance(vec3 N, vec3 V, vec3 wp, vec3 albedo, float metallic, float roughness) {
    vec3 sum = vec3(0.0);
    for (int i = 0; i < lights.counts.x; ++i)
        sum += lightContribution(i, N, V, wp, albedo, metallic, roughness).specular;
    return sum;
}

// Full realtime lighting in a single loop (diffuse incl. ambient, + specular).
LightTerms accumulate(vec3 N, vec3 V, vec3 wp, vec3 albedo, float metallic, float roughness) {
    LightTerms total;
    total.diffuse = lights.ambient.rgb * albedo;
    total.specular = vec3(0.0);
    for (int i = 0; i < lights.counts.x; ++i) {
        LightTerms t = lightContribution(i, N, V, wp, albedo, metallic, roughness);
        total.diffuse  += t.diffuse;
        total.specular += t.specular;
    }
    return total;
}
