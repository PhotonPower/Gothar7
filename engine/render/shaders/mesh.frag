#version 450 core
// Base colour (factor x sRGB texture) with half-Lambert from a fixed direction: enough to read
// shapes until real lights (M2). Lighting happens in linear space, the result is sRGB-encoded.
#include "common/color.glsl"

layout(binding = 0) uniform sampler2D uBaseColorTexture; // sRGB; 1x1 white if the material has none

uniform vec4 uBaseColor; // linear factor

in vec3 vNormal;
in vec2 vUv;
out vec4 fragColor;

const vec3 kLightDirection = normalize(vec3(0.4, 1.0, 0.3));

void main()
{
    const vec4 base = uBaseColor * texture(uBaseColorTexture, vUv);
    const float halfLambert = dot(normalize(vNormal), kLightDirection) * 0.5 + 0.5;
    const vec3 lit = base.rgb * (0.25 + 0.75 * halfLambert * halfLambert);
    fragColor = vec4(linearToSrgb(lit), base.a);
}
