#version 450 core
// Grass (M17 G1): blades lit like the terrain under them (its normal, sun shadows, point lights, fog). The cards
// (stage 1) cut six blades of differing height out of their quad (alpha test). With SHADOW only the depth.
#ifndef SHADOW
#include "common/lighting.glsl"
#include "common/fog.glsl"
#endif

uniform int uStage;
#ifndef SHADOW
uniform vec3 uCameraPosition;
#endif

in vec3 vWorldPosition;
in vec2 vCard;
#ifndef SHADOW
in vec3 vNormal;
in vec3 vColour;
out vec4 fragColor;
#endif

// Coverage of the card at (u across -1..1, v up 0..1): six tapering blades, leaning alternately.
bool cardBlade(vec2 c)
{
    const float u = c.x * 0.5 + 0.5;
    const float v = c.y;
    for (int i = 0; i < 6; ++i)
    {
        const float fi = float(i);
        const float top = 0.6 + 0.4 * fract(fi * 0.618 + 0.3);
        if (v > top)
        {
            continue;
        }
        const float centre = 0.1 + 0.16 * fi;
        const float lean = (i % 2 == 0 ? 0.12 : -0.1) * v * v;
        const float halfWidth = 0.055 * (1.0 - v / top);
        if (abs(u - centre - lean) < halfWidth)
        {
            return true;
        }
    }
    return false;
}

void main()
{
    if (uStage == 1 && !cardBlade(vCard))
    {
        discard;
    }
#ifndef SHADOW
    const vec3 n = normalize(vNormal);
    vec3 colour = vColour * incomingLight(vWorldPosition, n);
    colour = applyFog(colour, distance(vWorldPosition, uCameraPosition));
    fragColor = vec4(colour, 1.0);
#endif
}
