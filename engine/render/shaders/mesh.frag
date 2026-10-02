#version 450 core
// Stylised material (render.md): base colour, tangent-space normal map, emissive, alpha test
// (#define ALPHA_TEST) – lit by a fixed half-Lambert direction until the light task. Lighting
// happens in linear space, the result is sRGB-encoded until the tonemapping pass exists.
#include "common/color.glsl"

layout(binding = 0) uniform sampler2D uBaseColorTexture; // sRGB; white if none
layout(binding = 1) uniform sampler2D uNormalTexture;    // linear; flat (0.5, 0.5, 1) if none
layout(binding = 2) uniform sampler2D uEmissiveTexture;  // sRGB; white if none (factor decides)

uniform vec4 uBaseColor;    // linear factor
uniform vec3 uEmissive;     // linear factor
uniform float uNormalScale;
uniform float uAlphaCutoff;

in vec3 vNormal;
in vec4 vTangent;
in vec2 vUv;
out vec4 fragColor;

const vec3 kLightDirection = normalize(vec3(0.4, 1.0, 0.3));

vec3 surfaceNormal()
{
    vec3 n = normalize(vNormal);
    if (!gl_FrontFacing)
    {
        n = -n; // double-sided materials: the back face is lit like a front face
    }
    if (dot(vTangent.xyz, vTangent.xyz) < 1e-8)
    {
        return n; // mesh without tangents (no normal map in its material)
    }
    const vec3 t = normalize(vTangent.xyz - n * dot(n, vTangent.xyz));
    const vec3 b = cross(n, t) * vTangent.w;
    vec3 mapped = texture(uNormalTexture, vUv).xyz * 2.0 - 1.0;
    mapped.xy *= uNormalScale;
    return normalize(t * mapped.x + b * mapped.y + n * mapped.z);
}

void main()
{
    const vec4 base = uBaseColor * texture(uBaseColorTexture, vUv);
#ifdef ALPHA_TEST
    if (base.a < uAlphaCutoff)
    {
        discard;
    }
#endif
    const float halfLambert = dot(surfaceNormal(), kLightDirection) * 0.5 + 0.5;
    vec3 color = base.rgb * (0.25 + 0.75 * halfLambert * halfLambert);
    color += uEmissive * texture(uEmissiveTexture, vUv).rgb;
    fragColor = vec4(linearToSrgb(color), base.a);
}
