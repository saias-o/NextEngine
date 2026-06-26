#version 450

#ifdef MULTIVIEW
#extension GL_EXT_multiview : require
#endif

layout(set = 0, binding = 0) uniform CameraUBO {
    mat4 view[2];
    mat4 proj[2];
} cam;

struct Particle {
    vec4 positionSize;
    vec4 color;
    vec4 rotationStretch;
};

layout(std430, set = 1, binding = 0) readonly buffer ParticleBuffer {
    Particle particles[];
};

layout(push_constant) uniform PushConstants {
    uint particleOffset;
} push;

layout(location = 0) out vec4 fragColor;
layout(location = 1) out vec2 fragUV;

const vec2 CORNERS[6] = vec2[6](
    vec2(-0.5, -0.5), vec2( 0.5, -0.5), vec2( 0.5,  0.5),
    vec2(-0.5, -0.5), vec2( 0.5,  0.5), vec2(-0.5,  0.5)
);

void main() {
    uint particleIndex = push.particleOffset + uint(gl_VertexIndex / 6);
    uint cornerIndex = uint(gl_VertexIndex % 6);

#ifdef MULTIVIEW
    int viewIndex = gl_ViewIndex;
#else
    int viewIndex = 0;
#endif

    Particle p = particles[particleIndex];
    mat3 invViewRot = transpose(mat3(cam.view[viewIndex]));
    vec3 right = normalize(invViewRot[0]);
    vec3 up = normalize(invViewRot[1]);

    vec2 corner = CORNERS[cornerIndex];
    float rotation = p.rotationStretch.x;
    float stretchY = max(p.rotationStretch.y, 1.0);
    if (p.rotationStretch.z > 0.5) {
        vec3 fallDir = vec3(0.0, -1.0, 0.0);
        vec2 projectedFall = vec2(dot(fallDir, right), dot(fallDir, up));
        float projectedLen = length(projectedFall);
        if (projectedLen > 0.0001) {
            projectedFall /= projectedLen;
            rotation = atan(-projectedFall.x, projectedFall.y);
        } else {
            rotation = 0.0;
        }
    }
    float c = cos(rotation);
    float s = sin(rotation);
    vec2 local = vec2(corner.x, corner.y * stretchY);
    local = vec2(local.x * c - local.y * s, local.x * s + local.y * c);
    vec3 world = p.positionSize.xyz + (right * local.x + up * local.y) * p.positionSize.w;
    gl_Position = cam.proj[viewIndex] * cam.view[viewIndex] * vec4(world, 1.0);

    fragColor = p.color;
    fragUV = corner + vec2(0.5);
}
