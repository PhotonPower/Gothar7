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
uniform float uAlphaCutoff;
uniform vec3 uCameraPosition; // for the fog distance

in vec3 vWorldPosition;
in vec3 vNormal;
in vec4 vTangent;
in vec2 vUv;
out vec4 fragColor;

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
    vec3 color = base.rgb * incomingLight(vWorldPosition, surfaceNormal());
    color += uEmissive * texture(uEmissiveTexture, vUv).rgb;
    color *= shadowDebugTint(vWorldPosition);
    color = applyFog(color, distance(vWorldPosition, uCameraPosition));
    fragColor = vec4(color, base.a); // linear HDR; the post pass tonemaps and encodes
}
