#version 450 core
// Particles: the procedural sprite's alpha times the particle's colour (linear HDR).

layout(binding = 0) uniform sampler2DArray uSprites;

in vec2 vUv;
in vec4 vColor;
flat in float vLayer;

out vec4 oColor;

void main()
{
    const float shape = texture(uSprites, vec3(vUv, vLayer)).a;
    const float a = shape * vColor.a;
    if (a < 0.003)
    {
        discard;
    }
    oColor = vec4(vColor.rgb, a);
}
