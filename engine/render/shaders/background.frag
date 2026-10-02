#version 450 core
// Background gradient behind the scene (placeholder for the sky, M4).
#include "common/color.glsl"

in vec2 vUv;
out vec4 fragColor;

void main()
{
    const float t = smoothstep(0.0, 1.0, vUv.y);
    vec3 color = mix(kDuskHorizon, kDuskZenith, t);
    color += (ditherNoise(gl_FragCoord.xy) - 0.5) / 255.0;
    fragColor = vec4(color, 1.0);
}
