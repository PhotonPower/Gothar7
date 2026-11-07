#version 450 core
// Final pass: bloom (M17 P1), exposure, colour grading (M17 P2, render::Grading), tonemapping, vignette, sRGB
// encoding of the linear HDR scene (render::PostProcess).
#include "common/color.glsl"

layout(binding = 0) uniform sampler2D uScene; // RGBA16F, linear
layout(binding = 1) uniform sampler2D uBloom; // the glow at half size (render::Bloom), linear

uniform float uExposure;
uniform int uTonemapper; // 0 ACES, 1 Reinhard, 2 none – same order as render::Tonemapper
uniform float uBloomIntensity; // 0: no bloom (the texture is then not read)
uniform int uGrade;            // 0: no grading (the image as before M17 P2)
uniform float uTemperature;
uniform float uTint;
uniform float uSaturation;
uniform float uContrast;
uniform vec3 uLift;
uniform vec3 uGamma;
uniform vec3 uGain;
uniform float uVignette;

const vec3 kLuma = vec3(0.2126, 0.7152, 0.0722);

// Same as render::gradeLinear: white balance, saturation, contrast around middle grey (linear HDR).
vec3 gradeLinear(vec3 c)
{
    vec3 balance = vec3(1.0 + 0.2 * uTemperature, 1.0 - 0.1 * uTint, 1.0 - 0.2 * uTemperature);
    balance /= dot(balance, kLuma);
    c = max(c, vec3(0.0)) * balance;
    c = max(mix(vec3(dot(c, kLuma)), c, uSaturation), vec3(0.0));
    return 0.18 * pow(c / 0.18, vec3(uContrast));
}

// Same as render::gradeDisplay: lift, gain, gamma on the display colour.
vec3 gradeDisplay(vec3 c)
{
    c = clamp(c, 0.0, 1.0);
    c = clamp(uGain * c + uLift * (1.0 - c), 0.0, 1.0);
    return pow(c, 1.0 / max(uGamma, vec3(0.01)));
}

// Same as render::vignetteFactor.
float vignette(vec2 uv)
{
    const float d = length((uv - 0.5) * vec2(1.0, 0.8)) * 1.41421356;
    const float t = clamp((d - 0.45) / (1.0 - 0.45), 0.0, 1.0);
    return 1.0 - uVignette * t * t * (3.0 - 2.0 * t);
}

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
    vec3 scene = texture(uScene, vUv).rgb;
    if (uBloomIntensity > 0.0)
    {
        scene += texture(uBloom, vUv).rgb * uBloomIntensity;
    }
    vec3 hdr = scene * uExposure;
    vec3 display;
    if (uGrade == 1)
    {
        display = gradeDisplay(tonemap(gradeLinear(hdr))) * vignette(vUv);
    }
    else
    {
        display = tonemap(hdr);
    }
    vec3 color = linearToSrgb(display);
    color += (ditherNoise(gl_FragCoord.xy) - 0.5) / 255.0; // break up 8-bit banding in dark gradients
    fragColor = vec4(color, 1.0);
}
