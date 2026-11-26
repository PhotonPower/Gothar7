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
    // Two sets of bands at different angles and speeds over a warped surface: uneven, not a striped ball.
    const float warp = sin(3.1 * n.x + time * 0.9) + 0.7 * sin(4.7 * n.z - time * 1.4) + 0.4 * sin(6.3 * n.y + time * 2.2);
    const float b1 = 0.5 + 0.5 * sin(9.0 * n.y + 2.2 * warp + time * 1.7);
    const float b2 = 0.5 + 0.5 * sin(13.0 * (0.6 * n.x + 0.8 * n.z) - 1.8 * warp - time * 2.9);
    const float bands = 1.3 * pow(max(0.65 * b1 + 0.55 * b2 - 0.2, 0.0), 3.0);
    const float wisps = pow(0.5 + 0.5 * sin(7.0 * n.x - 5.0 * n.z + 1.5 * warp + time * 2.6), 2.0);
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
