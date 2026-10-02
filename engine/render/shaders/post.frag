#version 450 core
// Final pass: exposure, tonemapping, sRGB encoding of the linear HDR scene (render::PostProcess).
#include "common/color.glsl"

layout(binding = 0) uniform sampler2D uScene; // RGBA16F, linear

uniform float uExposure;
uniform int uTonemapper; // 0 ACES, 1 Reinhard, 2 none – same order as render::Tonemapper

in vec2 vUv;
out vec4 fragColor;

vec3 tonemap(vec3 x)
{
    x = max(x, vec3(0.0));
    if (uTonemapper == 0)
    {
        // Narkowicz 2015 fit of the ACES filmic curve.
        return clamp((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14), 0.0, 1.0);
    }
    if (uTonemapper == 1)
    {
        return x / (1.0 + x);
    }
    return min(x, vec3(1.0));
}

void main()
{
    const vec3 hdr = texture(uScene, vUv).rgb * uExposure;
    vec3 color = linearToSrgb(tonemap(hdr));
    color += (ditherNoise(gl_FragCoord.xy) - 0.5) / 255.0; // break up 8-bit banding in dark gradients
    fragColor = vec4(color, 1.0);
}
