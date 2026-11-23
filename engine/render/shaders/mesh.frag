#version 450 core
// Stylised material (render.md): base colour, tangent-space normal map, emissive, alpha test
// (#define ALPHA_TEST), lit by common/lighting.glsl, fogged by common/fog.glsl. Output is linear
// HDR (render::SceneTarget); render::PostProcess tonemaps and encodes it.
#include "common/color.glsl"
#include "common/lighting.glsl"
#include "common/fog.glsl"

layout(binding = 0) uniform sampler2D uBaseColorTexture; // sRGB; white if none
layout(binding = 1) uniform sampler2D uNormalTexture;    // linear; flat (0.5, 0.5, 1) if none
layout(binding = 2) uniform sampler2D uEmissiveTexture;  // sRGB; white if none (factor decides)

uniform vec4 uBaseColor;    // linear factor
uniform vec3 uEmissive;     // linear factor
uniform float uNormalScale;
uniform int uNormalTwoChannel; // 1: BC5 normal map, only X and Y stored
uniform float uAlphaCutoff;
uniform vec3 uCameraPosition; // for the fog distance

in vec3 vWorldPosition;
in vec3 vNormal;
in vec4 vTangent;
in vec2 vUv;
out vec4 fragColor;
#ifdef SKINNED
// M17 part G: a spell's glow on a figure (render::FigureGlow, premultiplied colours; all zero: none).
in float vGlowHand;
uniform vec3 uGlowHand;
uniform vec3 uGlowRim;
uniform vec3 uGlowFill;
#endif

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
    if (uNormalTwoChannel != 0)
    {
        // Two-channel normal maps (BC5) store X and Y; the unit normal's Z follows from them.
        mapped.z = sqrt(max(0.0, 1.0 - dot(mapped.xy, mapped.xy)));
    }
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
    vec3 albedo = base.rgb;
    vec3 n = surfaceNormal();
    vec3 glint;
    const vec3 sky = wetSurface(vWorldPosition, albedo, n, glint); // M17 N: rain
    vec3 color = albedo * incomingLight(vWorldPosition, n);
    color += wetSheen(color, sky) + glint;
    color += uEmissive * texture(uEmissiveTexture, vUv).rgb;
#ifdef SKINNED
    const float facing = max(dot(n, normalize(uCameraPosition - vWorldPosition)), 0.0);
    const float rim = pow(1.0 - facing, 4.0);
    color += uGlowHand * vGlowHand + uGlowRim * rim + uGlowFill;
#endif
    color *= shadowDebugTint(vWorldPosition);
    color = applyFog(color, distance(vWorldPosition, uCameraPosition));
    fragColor = vec4(color, base.a); // linear HDR; the post pass tonemaps and encodes
}
