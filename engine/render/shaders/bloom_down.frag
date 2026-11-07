#version 450 core
// Bloom down step (render::Bloom, M17 P1): 13 taps around the pixel (Jimenez, "Next Generation Post Processing in
// Call of Duty: Advanced Warfare"), weighted as five overlapping boxes; the first step keeps only what glows.

layout(binding = 0) uniform sampler2D uSource;

uniform vec2 uTexel;      // one texel of the source
uniform int uPrefilter;   // 1: the scene - apply the threshold
uniform float uThreshold; // luminance from which a pixel glows
uniform float uKnee;      // soft transition below it

in vec2 vUv;
out vec4 fragColor;

vec3 prefilter(vec3 color)
{
    // Same as render::bloomPrefilter.
    const float brightness = max(color.r, max(color.g, color.b));
    const float k = max(uThreshold * uKnee, 1e-5);
    float soft = clamp(brightness - uThreshold + k, 0.0, 2.0 * k);
    soft = soft * soft / (4.0 * k);
    const float contribution = max(soft, brightness - uThreshold) / max(brightness, 1e-5);
    return color * max(contribution, 0.0);
}

vec3 tap(vec2 offset)
{
    return texture(uSource, vUv + offset * uTexel).rgb;
}

void main()
{
    const vec3 a = tap(vec2(-2.0, -2.0));
    const vec3 b = tap(vec2(0.0, -2.0));
    const vec3 c = tap(vec2(2.0, -2.0));
    const vec3 d = tap(vec2(-1.0, -1.0));
    const vec3 e = tap(vec2(1.0, -1.0));
    const vec3 f = tap(vec2(-2.0, 0.0));
    const vec3 g = tap(vec2(0.0, 0.0));
    const vec3 h = tap(vec2(2.0, 0.0));
    const vec3 i = tap(vec2(-1.0, 1.0));
    const vec3 j = tap(vec2(1.0, 1.0));
    const vec3 k = tap(vec2(-2.0, 2.0));
    const vec3 l = tap(vec2(0.0, 2.0));
    const vec3 m = tap(vec2(2.0, 2.0));
    vec3 color = (d + e + i + j) * 0.125;
    color += (a + b + f + g) * 0.03125;
    color += (b + c + g + h) * 0.03125;
    color += (f + g + k + l) * 0.03125;
    color += (g + h + l + m) * 0.03125;
    if (uPrefilter == 1)
    {
        color = prefilter(color);
    }
    fragColor = vec4(max(color, vec3(0.0)), 1.0);
}
