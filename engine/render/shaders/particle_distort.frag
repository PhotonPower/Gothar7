#version 450 core
// Distorting particles (M17 part V, render::DistortionTarget): each writes a screen offset (fraction of the
// picture) into the offset buffer, depth-tested against the scene. Same formulas as render::distortOffset.
// vColor: r strength, g age 0..1, b shape (0 haze, 1 ring, 2 swirl).

in vec2 vUv;
in vec4 vColor;
flat in float vLayer;

out vec4 oOffset;

void main()
{
    const vec2 p = vUv * 2.0 - 1.0;
    const float r = length(p);
    if (r >= 1.0)
    {
        discard;
    }
    const float strength = vColor.r;
    const float age = vColor.g;
    const int shape = int(vColor.b + 0.5);
    const float falloff = (1.0 - r) * (1.0 - r);
    vec2 offset = vec2(0.0);
    if (shape == 0)
    {
        offset = vec2(sin(p.y * 7.0 + age * 23.0), cos(p.x * 6.0 - age * 19.0)) * (0.004 * strength * falloff);
    }
    else if (r > 1e-4)
    {
        if (shape == 1)
        {
            const float d = (r - age) / 0.15;
            offset = p / r * (0.01 * strength * exp(-d * d) * (1.0 - age));
        }
        else
        {
            offset = vec2(-p.y, p.x) / r * (0.006 * strength * falloff);
        }
    }
    oOffset = vec4(offset, 0.0, 1.0); // additive (src * 1 + dst): offsets of overlapping effects add up
}
