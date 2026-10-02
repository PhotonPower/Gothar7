#version 450 core
// Depth only; with ALPHA_TEST, foliage casts holed shadows like it is drawn.

#ifdef ALPHA_TEST
layout(binding = 0) uniform sampler2D uBaseColorTexture;
uniform vec4 uBaseColor;
uniform float uAlphaCutoff;
in vec2 vUv;
#endif

void main()
{
#ifdef ALPHA_TEST
    if ((uBaseColor * texture(uBaseColorTexture, vUv)).a < uAlphaCutoff)
    {
        discard;
    }
#endif
}
