#version 450 core
// Debug lines: hidden parts are dashed and faint when depth-tested.
#include "common/debug.glsl"

uniform int uDepthTest;

in vec4 vColor;
out vec4 fragColor;

void main()
{
    vec4 color = vColor;
    if (uDepthTest != 0 && debugHidden(gl_FragCoord.z))
    {
        if (mod(floor((gl_FragCoord.x + gl_FragCoord.y) / 4.0), 2.0) < 1.0)
        {
            discard;
        }
        color.a *= 0.35;
    }
    fragColor = color;
}
