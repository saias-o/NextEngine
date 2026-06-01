#version 450

struct PointLight {
    vec4 position;  // xyz = world position, w = range
    vec4 color;     // rgb = color, w = intensity
};

// Set 0: per-frame global data.
layout(set = 0, binding = 1) uniform LightingUBO {
    vec4 ambient;       // rgb = ambient color
    vec4 cameraPos;     // xyz = camera world position
    vec4 dirDirection;  // xyz = travel direction, w = intensity
    vec4 dirColor;      // rgb = color
    PointLight pointLights[4];
    ivec4 counts;       // x = active point lights, y = mode (0 realtime, 1 baked)
} lights;

// Set 1: per-material data.
layout(set = 1, binding = 0) uniform sampler2D texSampler;
layout(set = 1, binding = 1) uniform MaterialUBO {
    vec4 baseColor;  // rgb tint, a unused (for now)
} material;

layout(location = 0) in vec3 fragWorldPos;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec3 fragColor;
layout(location = 3) in vec2 fragTexCoord;

layout(location = 0) out vec4 outColor;

// Blinn-Phong contribution for a single light direction (pointing toward light).
vec3 blinnPhong(vec3 N, vec3 V, vec3 L, vec3 radiance) {
    float diffuse = max(dot(N, L), 0.0);
    vec3 H = normalize(L + V);
    float specular = pow(max(dot(N, H), 0.0), 32.0) * 0.3;
    return radiance * (diffuse + specular);
}

void main() {
    vec3 albedo = texture(texSampler, fragTexCoord).rgb * fragColor * material.baseColor.rgb;
    vec3 N = normalize(fragNormal);
    vec3 V = normalize(lights.cameraPos.xyz - fragWorldPos);

    // --- Indirect term ---
    // Today: flat ambient. Baked path (mode == 1) would sample a precomputed
    // lightmap here instead, so static indirect light comes "for free".
    vec3 indirect = lights.ambient.rgb;

    // --- Direct (realtime) term ---
    vec3 direct = vec3(0.0);

    // Directional light.
    direct += blinnPhong(N, V, normalize(-lights.dirDirection.xyz),
                         lights.dirColor.rgb * lights.dirDirection.w);

    // Point lights with smooth range-based attenuation.
    for (int i = 0; i < lights.counts.x; ++i) {
        vec3 toLight = lights.pointLights[i].position.xyz - fragWorldPos;
        float dist = length(toLight);
        vec3 L = toLight / max(dist, 1e-4);
        float range = lights.pointLights[i].position.w;
        float atten = clamp(1.0 - (dist * dist) / (range * range), 0.0, 1.0);
        atten *= atten;
        direct += blinnPhong(N, V, L, lights.pointLights[i].color.rgb
                                       * lights.pointLights[i].color.w) * atten;
    }

    outColor = vec4(albedo * (indirect + direct), 1.0);
}
