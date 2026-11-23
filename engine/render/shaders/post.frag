#version 450 core
// Final pass: bloom (M17 P1), exposure, colour grading (M17 P2, render::Grading), tonemapping, vignette, sRGB
// encoding of the linear HDR scene (render::PostProcess).
#include "common/color.glsl"

layout(binding = 0) uniform sampler2D uScene; // RGBA16F, linear
layout(binding = 1) uniform sampler2D uBloom; // the glow at half size (render::Bloom), linear

uniform float uExposure;
uniform int uTonemapper; // 0 ACES, 1 Reinhard, 2 none – same order as render::Tonemapper
uniform float uBloomIntensity; // 0: no bloom (the texture is then not read)
uniform int uUnderwater;       // 1: the camera under water (M17 W3)
uniform vec3 uWaterAbsorption;
uniform vec3 uWaterFog;
uniform float uWaterDistortion;
uniform float uWaterTime;
uniform mat4 uInverseProjection;
layout(binding = 2) uniform sampler2D uDepth; // the scene's depth (reverse-Z), read under water only
layout(binding = 3) uniform sampler2D uDistortion; // M17 V: screen offsets in rg (render::DistortionTarget)
uniform int uDistort;      // 1: read uDistortion (else the image as before)
uniform float uWobble;     // M17 V: the whole picture wobbles by this much (0: not)
uniform float uWobbleTime;
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
    vec2 uv = vUv;
    if (uDistort == 1)
    {
        uv += texture(uDistortion, vUv).rg; // heat haze, shock rings, swirls of spells
    }
    if (uWobble > 0.0)
    {
        uv += uWobble * vec2(sin(vUv.y * 20.0 + uWobbleTime * 8.0), cos(vUv.x * 18.0 + uWobbleTime * 7.0));
    }
    uv = clamp(uv, 0.0, 1.0);
    vec3 scene;
    if (uUnderwater == 1)
    {
        // Under water (render::underwaterColor): a slight wavering, then tint and fog by the distance.
        uv = clamp(uv + uWaterDistortion * 0.004 *
                            vec2(sin(vUv.y * 30.0 + uWaterTime * 1.7), cos(vUv.x * 25.0 + uWaterTime * 1.3)),
                   0.0, 1.0);
        const vec4 view = uInverseProjection * vec4(uv * 2.0 - 1.0, texture(uDepth, uv).r, 1.0);
        const vec3 t = exp(-uWaterAbsorption * length(view.xyz / view.w));
        scene = texture(uScene, uv).rgb * t + uWaterFog * (1.0 - t);
    }
    else
    {
        scene = texture(uScene, uv).rgb;
    }
    if (uBloomIntensity > 0.0)
    {
        scene += texture(uBloom, uv).rgb * uBloomIntensity;
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
