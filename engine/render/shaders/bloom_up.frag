#version 450 core
// Bloom up step (render::Bloom, M17 P1): a 3x3 tent filter of the smaller level, added onto the larger one
// (the pipeline blends additively).

layout(binding = 0) uniform sampler2D uSource;

uniform vec2 uTexel; // one texel of the source (the smaller level)

in vec2 vUv;
out vec4 fragColor;

void main()
{
    vec3 color = texture(uSource, vUv).rgb * 4.0;
    color += texture(uSource, vUv + vec2(-1.0, 0.0) * uTexel).rgb * 2.0;
    color += texture(uSource, vUv + vec2(1.0, 0.0) * uTexel).rgb * 2.0;
    color += texture(uSource, vUv + vec2(0.0, -1.0) * uTexel).rgb * 2.0;
    color += texture(uSource, vUv + vec2(0.0, 1.0) * uTexel).rgb * 2.0;
    color += texture(uSource, vUv + vec2(-1.0, -1.0) * uTexel).rgb;
    color += texture(uSource, vUv + vec2(1.0, -1.0) * uTexel).rgb;
    color += texture(uSource, vUv + vec2(-1.0, 1.0) * uTexel).rgb;
    color += texture(uSource, vUv + vec2(1.0, 1.0) * uTexel).rgb;
    fragColor = vec4(color / 16.0, 1.0);
}
