#version 450 core
// Blur of the ambient occlusion (M17 P3, render::Ssao): 4 x 4 around the pixel, weighted by how close the
// neighbour's depth is (edges between near and far stay sharp).

layout(binding = 0) uniform sampler2D uAo;    // half resolution
layout(binding = 1) uniform sampler2D uDepth; // full resolution, reverse-Z

uniform mat4 uInverseProjection;
uniform vec2 uTexel; // of the half resolution

in vec2 vUv;
out float fragAo;

float viewDepth(vec2 uv)
{
    const vec4 p = uInverseProjection * vec4(uv * 2.0 - 1.0, texture(uDepth, uv).r, 1.0);
    return -p.z / p.w;
}

void main()
{
    const float centre = viewDepth(vUv);
    float sum = 0.0;
    float weights = 0.0;
    for (int y = -2; y < 2; ++y)
    {
        for (int x = -2; x < 2; ++x)
        {
            const vec2 uv = vUv + (vec2(x, y) + 0.5) * uTexel;
            const float w = 1.0 / (1e-3 + abs(viewDepth(uv) - centre) / max(centre, 1e-3) * 50.0);
            sum += texture(uAo, uv).r * w;
            weights += w;
        }
    }
    fragAo = sum / max(weights, 1e-5);
}
