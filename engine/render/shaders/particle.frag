#version 450 core
// Particles: the procedural sprite's alpha times the particle's colour (linear HDR).

layout(binding = 0) uniform sampler2DArray uSprites;

in vec2 vUv;
in vec4 vColor;
flat in float vLayer;
flat in vec3 vMotion;

out vec4 oColor;

// The light spell's orb: a sphere of plasma - a dim core, a bright limb, bands drifting round it (layer 3).
float plasma(vec2 uv, float time)
{
    const vec2 p = uv * 2.0 - 1.0;
    const float r = length(p);
    if (r > 1.0)
    {
        return 0.0;
    }
    const float z = sqrt(1.0 - r * r);
    const float a = time * 0.7;
    const vec3 n = vec3(p.x * cos(a) + z * sin(a), p.y, -p.x * sin(a) + z * cos(a)); // turning slowly
    // Sharp bright bands over a dim body: it must read as plasma, not as a flat disc.
    const float bands = pow(0.5 + 0.5 * sin(11.0 * n.y + 2.5 * sin(4.0 * n.x + time * 1.3) + time * 1.9), 4.0);
    const float wisps = pow(0.5 + 0.5 * sin(7.0 * n.x - 5.0 * n.z + time * 2.6), 2.0);
    const float limb = pow(1.0 - z, 3.0);
    const float core = pow(z, 6.0);
    return (0.12 + 0.25 * core + 0.8 * limb + 1.6 * bands * (0.4 + 0.6 * wisps) * z) * smoothstep(1.0, 0.9, r);
}

void main()
{
    if (vLayer > 2.5)
    {
        const float glow = plasma(vUv, vMotion.x) * vColor.a;
        if (glow < 0.003)
        {
            discard;
        }
        oColor = vec4(vColor.rgb, glow);
        return;
    }
    const float shape = texture(uSprites, vec3(vUv, vLayer)).a;
    const float a = shape * vColor.a;
    if (a < 0.003)
    {
        discard;
    }
    oColor = vec4(vColor.rgb, a);
}
