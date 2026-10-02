#version 450 core
// Terrain surface: up to 8 splat layers (or slope/height colours without a splat map), holes, lit
// and fogged like meshes. With SHADOW only the holes are cut (depth pass).
#ifndef SHADOW
#include "common/color.glsl"
#include "common/lighting.glsl"
#include "common/fog.glsl"
#endif

layout(binding = 7) uniform sampler2D uHoles; // R8, one texel per cell; 0 = hole
uniform int uHasHoles;

in vec2 vSample;

#ifndef SHADOW
layout(binding = 5) uniform sampler2DArray uSplat;  // weights: map m, channel k = layer 4m + k
layout(binding = 6) uniform sampler2DArray uLayers; // layer albedos (sRGB)

uniform vec3 uCameraPosition;
uniform vec2 uHeightRange;    // minY, maxY
uniform int uLayerCount;      // 0 = no splat map
uniform float uTiles[8];      // metres per texture repeat
uniform vec4 uSplatTransform; // samples -> splat uv: xy scale, zw offset (pixel centres on samples)

in vec3 vWorldPosition;
in vec3 vNormal;
out vec4 fragColor;

vec3 slopeAlbedo(vec3 n)
{
    // Linear albedos: grass on gentle slopes, earth on steeper ones, rock on cliffs.
    const vec3 grass = vec3(0.10, 0.16, 0.05);
    const vec3 earth = vec3(0.17, 0.12, 0.07);
    const vec3 rock = vec3(0.22, 0.21, 0.19);
    const float slope = 1.0 - n.y; // 0 = flat
    vec3 albedo = mix(grass, earth, smoothstep(0.08, 0.2, slope));
    albedo = mix(albedo, rock, smoothstep(0.3, 0.5, slope));
    // A little variation with height so plains do not look flat.
    const float h = (vWorldPosition.y - uHeightRange.x) / max(uHeightRange.y - uHeightRange.x, 1e-3);
    return albedo * mix(0.85, 1.1, h);
}

vec3 splatAlbedo()
{
    const vec2 uv = vSample * uSplatTransform.xy + uSplatTransform.zw;
    const vec4 first = texture(uSplat, vec3(uv, 0.0));
    const vec4 second = uLayerCount > 4 ? texture(uSplat, vec3(uv, 1.0)) : vec4(0.0);
    const float weights[8] = float[8](first.r, first.g, first.b, first.a, second.r, second.g, second.b, second.a);
    vec3 sum = vec3(0.0);
    float total = 0.0;
    vec3 base = vec3(0.0);
    // Every layer is sampled (uniform control flow keeps the implicit derivatives valid).
    for (int i = 0; i < uLayerCount; ++i)
    {
        const vec3 albedo = texture(uLayers, vec3(vWorldPosition.xz / uTiles[i], float(i))).rgb;
        base = i == 0 ? albedo : base;
        sum += weights[i] * albedo;
        total += weights[i];
    }
    // Weights are normalised; nowhere any weight means layer 0.
    return total > 1e-3 ? sum / total : base;
}
#endif

void main()
{
    if (uHasHoles != 0)
    {
        const ivec2 size = textureSize(uHoles, 0);
        if (texelFetch(uHoles, clamp(ivec2(floor(vSample)), ivec2(0), size - 1), 0).r < 0.5)
        {
            discard;
        }
    }
#ifndef SHADOW
    const vec3 n = normalize(vNormal);
    const vec3 albedo = uLayerCount > 0 ? splatAlbedo() : slopeAlbedo(n);
    vec3 color = albedo * incomingLight(vWorldPosition, n);
    color *= shadowDebugTint(vWorldPosition);
    color = applyFog(color, distance(vWorldPosition, uCameraPosition));
    fragColor = vec4(color, 1.0);
#endif
}
