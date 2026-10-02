#version 450 core
// Debug text: 8x8 bitmap font (R8 atlas); text hidden behind the scene is drawn faint.
#include "common/debug.glsl"

layout(binding = 1) uniform sampler2D uFont;

in vec2 vUv;
in vec4 vColor;
in float vDepth;
out vec4 fragColor;

void main()
{
    vec4 color = vColor;
    color.a *= texture(uFont, vUv).r;
    if (color.a <= 0.0)
    {
        discard;
    }
    if (debugHidden(vDepth))
    {
        color.a *= 0.35;
    }
    fragColor = color;
}
