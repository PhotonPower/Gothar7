#version 450 core
// Base colour with half-Lambert from a fixed direction: enough to read shapes until real lights (M2).

uniform vec4 uBaseColor;

in vec3 vNormal;
in vec2 vUv;
out vec4 fragColor;

const vec3 kLightDirection = normalize(vec3(0.4, 1.0, 0.3));

void main()
{
    const float halfLambert = dot(normalize(vNormal), kLightDirection) * 0.5 + 0.5;
    fragColor = vec4(uBaseColor.rgb * (0.25 + 0.75 * halfLambert * halfLambert), uBaseColor.a);
}
