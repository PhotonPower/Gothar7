#version 450 core
// Rain (M17 weather part R): faint additive streaks, soft at their sides; splashes as small thin rings.

uniform vec3 uLight; // sky and sun on the drops (linear)
uniform int uSplash;

in float vAlpha;
in vec2 vLocal;
out vec4 fragColor;

void main()
{
    float shape;
    if (uSplash == 0)
    {
        shape = (1.0 - abs(vLocal.x)) * smoothstep(0.0, 0.3, vLocal.y); // the tail end fades in
    }
    else
    {
        const float r = length(vLocal);
        shape = smoothstep(1.0, 0.8, r) * smoothstep(0.5, 0.8, r);
    }
    const float a = vAlpha * shape;
    if (a <= 0.001)
    {
        discard;
    }
    fragColor = vec4(uLight * 0.35, a);
}
