// Shared water data + shore helpers for water.vert and water.frag.
//
// All look/feel lives in a small set-1 UBO array (one entry per WaterNode, indexed
// by a push-constant). This replaces the old 128-byte push block: a realistic shore
// needs more parameters than the Vulkan-guaranteed push minimum can hold, and a UBO
// is the engine's standard "per-frequency" home for per-object data (like Material's
// set 1). The push now only carries the tiny per-draw bits (node index + time).
//
// The shore is fully ANALYTIC: `waterDepthAt` returns the local water depth in metres
// from a couple of parameters, so making a beach or a lake is pure data — no textures,
// no depth-buffer reads (perfect for tiled mobile/VR GPUs).

const int WATER_MAX = 8;  // must match kMaxWaters in WaterFeature

struct GpuWater {
    vec4 area;       // x=centreX, y=surfaceY, z=centreZ, w=halfSize
    vec4 deep;       // rgb deep-water colour, w=roughness
    vec4 foam;       // rgb foam colour,       w=reflectivity
    vec4 waveA;      // x=amplitude, y=wavelength, z=speed, w=choppiness
    vec4 detail1;    // x=scale, y=speed, z=strength, w=angleDeg   (normal layer 1)
    vec4 detail2;    // x=scale, y=speed, z=strength, w=angleDeg   (normal layer 2)
    vec4 look;       // x=fresnelPower, y=specularPower, z=specularIntensity, w=foamThreshold
    vec4 misc;       // x=warpAmount, y=detailFadeDistance, z=foamIntensity, w=depthColorFalloff
    vec4 shoreColor; // rgb shallow-water colour, w=edgeFadeDepth
    vec4 shoreGeom;  // beach: (dirX,dirZ,waterlineDist,slope) | lake: (centreX,centreZ,radius,slope)
    vec4 shoreTune;  // x=foamWidth, y=swashSpeed, z=swashAmount, w=waveFlattenDepth
    vec4 shoreMode;  // x=mode (0 none/1 beach/2 lake), y=shoreFoamIntensity, z/w reserved
};

layout(set = 1, binding = 0) uniform WaterBlock {
    GpuWater items[WATER_MAX];
} waters;

layout(push_constant) uniform WaterPush {
    uint index;   // which entry in waters.items this draw uses
    float time;   // animation clock (s)
} push;

// Local water depth (m) at a world XZ position: surfaceY - seabedY.
//   > 0  underwater   (deepens away from the waterline)
//   = 0  exactly the waterline
//   < 0  dry land      (water should fade out / be discarded here)
// Both shore shapes reduce to depth = (horizontal distance to waterline) * slope.
float waterDepthAt(vec2 xz, GpuWater w) {
    int mode = int(w.shoreMode.x + 0.5);
    if (mode == 0) return 1e4;                          // open water: always deep
    if (mode == 1) {                                    // beach: straight shoreline
        vec2 dir = normalize(w.shoreGeom.xy);           // points inland
        float inland = dot(xz - w.area.xz, dir);        // distance toward the land
        return (w.shoreGeom.z - inland) * w.shoreGeom.w;
    }
    float dist = length(xz - w.shoreGeom.xy);           // mode 2: lake (radial)
    return (w.shoreGeom.z - dist) * w.shoreGeom.w;
}

vec2 rotate2(vec2 v, float rad) {
    float s = sin(rad), c = cos(rad);
    return vec2(c * v.x - s * v.y, s * v.x + c * v.y);
}

// Direction the swell travels — toward the shore, so waves roll INTO the beach with
// no authoring. Beach: the inland normal. Lake: radially outward toward the rim. With
// no shore: a fixed open-ocean heading (keeps the original endless-water look).
vec2 waveDirAt(vec2 xz, GpuWater w) {
    int mode = int(w.shoreMode.x + 0.5);
    if (mode == 1) return normalize(w.shoreGeom.xy);
    if (mode == 2) {
        vec2 d = xz - w.shoreGeom.xy;
        float l = length(d);
        return l > 1e-4 ? d / l : vec2(1.0, 0.0);
    }
    return normalize(vec2(0.9, 0.35));
}
