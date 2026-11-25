// Window glass (M17 windows part F1, render.md "Spiegelnde Fenster"): a dark pane that mirrors the sky by
// Fresnel (Schlick, f0 0.04, at most 0.5 at grazing views), the sun's glint, and the panes of the time around
// 1700: bullseye panes (one bulge per small round pane) or diamond panes in lead, slightly green and wavy.
// Stage A of render::WindowReflections; probes (B) and SSR (C) come later. Needs common/lighting.glsl.

const float kGlassF0 = 0.04;
const float kGlassMaxFresnel = 0.5;

// Same as render::glassFresnel.
float glassFresnel(float cosTheta)
{
    const float m = 1.0 - clamp(cosTheta, 0.0, 1.0);
    return min(kGlassF0 + (1.0 - kGlassF0) * m * m * m * m * m, kGlassMaxFresnel);
}

float glassHash(vec2 p)
{
    p = fract(p * vec2(0.1031, 0.1030));
    p += dot(p, p.yx + 33.33);
    return fract((p.x + p.y) * p.x);
}

// The sky in direction r as background.frag draws it without sun disc and stars; below the horizon the
// street and the houses opposite, taken as a darker horizon.
vec3 glassSky(vec3 r)
{
    const vec3 horizon = uLighting.fogColorStart.rgb;
    if (r.y < 0.0)
    {
        return horizon * mix(0.6, 0.25, smoothstep(0.0, 0.5, -r.y));
    }
    return mix(horizon, uLighting.skyZenith.rgb, smoothstep(0.0, 0.6, r.y));
}

// The pane's surface: the perturbed normal and how much of it is lead (0 glass .. 1 lead).
// pattern (render::Material uGlass.x): 1 bullseye, 2 diamond, 3 plain.
vec3 glassPane(vec3 worldPosition, vec3 n, int pattern, out float lead)
{
    lead = 0.0;
    // Axes in the pane: horizontal along it and up (for walls); any for horizontal panes.
    const vec3 helper = abs(n.y) < 0.9 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
    const vec3 t = normalize(cross(helper, n));
    const vec3 b = cross(n, t);
    const vec2 uv = vec2(dot(worldPosition, t), dot(worldPosition, b));
    vec2 slope = vec2(0.0);
    // Old glass is never flat: a slow waviness over every pane.
    slope += 0.03 * vec2(sin(uv.y * 23.0 + sin(uv.x * 7.0)), sin(uv.x * 19.0 + sin(uv.y * 5.0)));
    if (pattern == 1)
    {
        // Bullseye panes: round panes of 12 cm in rows offset by half a pane, lead in the gaps.
        const float size = 0.12;
        vec2 g = uv / size;
        g.x += 0.5 * mod(floor(g.y), 2.0);
        const vec2 cell = floor(g);
        const vec2 local = fract(g) - 0.5;
        const float r = length(local);
        slope += local * 1.1 * (1.0 - smoothstep(0.3, 0.46, r)); // the bulge
        slope += 0.15 * (glassHash(cell) - 0.5);                 // each pane sits a little askew
        lead = smoothstep(0.43, 0.48, r);
    }
    else if (pattern == 2)
    {
        // Diamond panes: 10 x 14 cm lozenges between lead cames.
        const vec2 d = vec2(uv.x / 0.10 + uv.y / 0.14, uv.y / 0.14 - uv.x / 0.10);
        const vec2 local = fract(d) - 0.5;
        const vec2 cell = floor(d);
        const float edge = max(abs(local.x), abs(local.y));
        slope += 0.12 * vec2(glassHash(cell) - 0.5, glassHash(cell + 17.0) - 0.5);
        lead = smoothstep(0.44, 0.48, edge);
    }
    return normalize(n + t * slope.x + b * slope.y);
}

// The glass's colour: `albedo` is the dark pane behind (the room), `tint` the glass's own colour.
vec3 glassColor(vec3 worldPosition, vec3 cameraPosition, vec3 n, vec3 albedo, vec3 tint, int pattern)
{
    const vec3 view = normalize(cameraPosition - worldPosition);
    if (!gl_FrontFacing)
    {
        // From inside (decision 6): milky and bright after the daylight, no mirror.
        return tint * mix(uLighting.ambientSky.rgb, uLighting.fogColorStart.rgb, 0.5) * 0.8;
    }
    float lead;
    const vec3 pane = glassPane(worldPosition, n, pattern, lead);
    const vec3 r = reflect(-view, pane);
    const float wet = wetness(worldPosition, n); // decision 10: wet panes shine more
    const float fresnel = min(glassFresnel(dot(pane, view)) * (1.0 + 0.6 * wet), kGlassMaxFresnel + 0.1 * wet);
    const vec3 behind = albedo * tint * incomingLight(worldPosition, n);
    const float sun = pow(max(dot(r, uLighting.sunDirection.xyz), 0.0), mix(300.0, 900.0, wet));
    const vec3 glint = uLighting.sunColor.rgb * sun * (1.5 + 2.0 * wet) * sunShadow(worldPosition, n);
    const vec3 glass = mix(behind, glassSky(r) * tint, fresnel) + glint * fresnel;
    const vec3 came = albedo * 0.5 * incomingLight(worldPosition, n); // lead: dull and dark
    return mix(glass, came, lead);
}
