#version 450 core
// Water (M17 W1): copies the scene's colour and depth, so the water pass can read what lies under the surface
// while drawing into the scene target (render::WaterRenderer).

layout(binding = 0) uniform sampler2D uColor; // RGBA16F, linear
layout(binding = 1) uniform sampler2D uDepth; // Depth32F, reverse-Z

layout(location = 0) out vec4 outColor;
layout(location = 1) out float outDepth;

void main()
{
    const ivec2 texel = ivec2(gl_FragCoord.xy);
    outColor = texelFetch(uColor, texel, 0);
    outDepth = texelFetch(uDepth, texel, 0).r;
}
