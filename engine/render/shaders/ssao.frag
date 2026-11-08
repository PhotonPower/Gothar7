#version 450 core
// Ambient occlusion (M17 P3, render::Ssao): at half resolution from the depth of the prepass. The normal comes
// from the depth (the flatter of the two neighbours per axis), 12 samples in the hemisphere around it, turned
// per pixel; a sample counts when the scene in front of it is nearer, weighted by the distance (no dark halos
// around objects far in front). Output: 1 open .. 0 fully occluded.

layout(binding = 0) uniform sampler2D uDepth; // reverse-Z, full resolution

uniform mat4 uProjection;
uniform mat4 uInverseProjection;
uniform vec2 uTexel; // of the full resolution depth
uniform float uRadius;
uniform float uStrength;
uniform float uKernel[36]; // render::ssaoKernel: 12 x (x, y, z)

in vec2 vUv;
out float fragAo;

vec3 viewAt(vec2 uv)
{
    const vec4 p = uInverseProjection * vec4(uv * 2.0 - 1.0, texture(uDepth, uv).r, 1.0);
    return p.xyz / p.w;
}

float hash(vec2 p)
{
    return fract(sin(dot(p, vec2(12.9898, 78.233))) * 43758.5453);
}

void main()
{
    if (texture(uDepth, vUv).r <= 0.0)
    {
        fragAo = 1.0; // the sky
        return;
    }
    const vec3 p = viewAt(vUv);
    const vec3 right = viewAt(vUv + vec2(uTexel.x, 0.0)) - p;
    const vec3 left = p - viewAt(vUv - vec2(uTexel.x, 0.0));
    const vec3 up = viewAt(vUv + vec2(0.0, uTexel.y)) - p;
    const vec3 down = p - viewAt(vUv - vec2(0.0, uTexel.y));
    const vec3 dx = abs(right.z) < abs(left.z) ? right : left;
    const vec3 dy = abs(up.z) < abs(down.z) ? up : down;
    vec3 n = normalize(cross(dx, dy));
    if (dot(n, -p) < 0.0)
    {
        n = -n;
    }
    // A basis around the normal, turned by a random angle per pixel.
    const float angle = hash(gl_FragCoord.xy) * 6.2831853;
    const vec3 r = vec3(cos(angle), sin(angle), 0.0);
    const vec3 t = normalize(r - n * dot(r, n) + vec3(1e-4, 0.0, 0.0));
    const mat3 tbn = mat3(t, cross(n, t), n);
    float occlusion = 0.0;
    for (int i = 0; i < 12; ++i)
    {
        const vec3 s = p + tbn * vec3(uKernel[3 * i], uKernel[3 * i + 1], uKernel[3 * i + 2]) * uRadius;
        const vec4 clip = uProjection * vec4(s, 1.0);
        const vec2 uv = clip.xy / clip.w * 0.5 + 0.5;
        const float scene = viewAt(uv).z;
        const float range = smoothstep(0.0, 1.0, uRadius / max(abs(p.z - scene), 1e-4));
        occlusion += (scene >= s.z + 0.03 ? 1.0 : 0.0) * range;
    }
    fragAo = clamp(1.0 - uStrength * occlusion / 12.0, 0.0, 1.0);
}
