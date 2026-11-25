// Window glass (M17 windows part F1, render.md "Spiegelnde Fenster"): a dark pane that mirrors the sky by
// Fresnel (Schlick, f0 0.04, at most 0.5 at grazing views), the sun's glint, and the panes of the time around
// 1700: bullseye panes (one bulge per small round pane) or diamond panes in lead, slightly green and wavy.
// Stage A of render::WindowReflections; with medium and high the reflection probes (B, render::ReflectionProbes)
// show the streets and houses around instead of the sky alone; SSR (C) comes later. Needs common/lighting.glsl.

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

// M17 windows F2: the reflection probes' cube faces as tiles in one atlas (render::ReflectionProbes); face f
// looks along kProbeFaceForward[f] with kProbeFaceUp[f] up, its x axis is cross(forward, up) - the same tables as
// render::kProbeFaceForward / kProbeFaceUp.
layout(binding = 10) uniform sampler2D uProbeAtlas;
const vec3 kProbeFaceForward[6] = vec3[6](vec3(1.0, 0.0, 0.0), vec3(-1.0, 0.0, 0.0), vec3(0.0, 1.0, 0.0),
                                          vec3(0.0, -1.0, 0.0), vec3(0.0, 0.0, 1.0), vec3(0.0, 0.0, -1.0));
const vec3 kProbeFaceUp[6] = vec3[6](vec3(0.0, 1.0, 0.0), vec3(0.0, 1.0, 0.0), vec3(0.0, 0.0, -1.0),
                                     vec3(0.0, 0.0, 1.0), vec3(0.0, 1.0, 0.0), vec3(0.0, 1.0, 0.0));
const float kProbeFadeStart = 25.0; // m from the nearest probe: its image fades into the sky alone
const float kProbeFadeEnd = 45.0;

// What probe `slot` sees in direction d.
vec3 probeSample(int slot, vec3 d)
{
    const vec3 a = abs(d);
    int face;
    if (a.x >= a.y && a.x >= a.z)
    {
        face = d.x > 0.0 ? 0 : 1;
    }
    else if (a.y >= a.z)
    {
        face = d.y > 0.0 ? 2 : 3;
    }
    else
    {
        face = d.z > 0.0 ? 4 : 5;
    }
    const vec3 forward = kProbeFaceForward[face];
    const vec3 up = kProbeFaceUp[face];
    const vec3 right = cross(forward, up);
    const float f = max(dot(d, forward), 1e-4);
    const float size = uLighting.probeParams.z;
    vec2 uv = 0.5 + 0.5 * vec2(dot(d, right), dot(d, up)) / f;
    uv = clamp(uv, vec2(0.5 / size), vec2(1.0 - 0.5 / size)); // stay inside the tile
    const int perRow = int(uLighting.probeParams.y + 0.5);
    const vec2 tile = vec2(float((slot % perRow) * 6 + face), float(slot / perRow));
    return textureLod(uProbeAtlas, (tile + uv) * size / vec2(textureSize(uProbeAtlas, 0)), 0.0).rgb;
}

// Parallax correction: where the ray from p along r leaves the probe's box, seen from the probe's centre.
vec3 probeDirection(int i, vec3 p, vec3 r)
{
    const vec3 lo = uLighting.probeBoxMin[i].xyz;
    const vec3 hi = uLighting.probeBoxMax[i].xyz;
    const vec3 safe = vec3(abs(r.x) < 1e-5 ? 1e-5 : r.x, abs(r.y) < 1e-5 ? 1e-5 : r.y, abs(r.z) < 1e-5 ? 1e-5 : r.z);
    const vec3 t = max((lo - p) / safe, (hi - p) / safe);
    const float exitDistance = max(min(min(t.x, t.y), t.z), 0.0);
    return normalize(p + r * exitDistance - uLighting.probeCenter[i].xyz);
}

// The world mirrored in direction r at p from the two nearest probes (blended by distance); `weight` how much it
// counts against the sky alone (0: no probe near).
vec3 probeReflection(vec3 p, vec3 r, out float weight)
{
    weight = 0.0;
    const int count = int(uLighting.probeParams.x + 0.5);
    if (count == 0)
    {
        return vec3(0.0);
    }
    int first = 0;
    int second = -1;
    float d1 = 1e30;
    float d2 = 1e30;
    for (int i = 0; i < count; ++i)
    {
        const vec3 d = uLighting.probeCenter[i].xyz - p;
        const float d2i = dot(d, d);
        if (d2i < d1)
        {
            second = first;
            d2 = d1;
            first = i;
            d1 = d2i;
        }
        else if (d2i < d2)
        {
            second = i;
            d2 = d2i;
        }
    }
    d1 = sqrt(d1);
    vec3 color = probeSample(int(uLighting.probeCenter[first].w + 0.5), probeDirection(first, p, r));
    if (second >= 0 && second != first)
    {
        d2 = sqrt(d2);
        const vec3 other = probeSample(int(uLighting.probeCenter[second].w + 0.5), probeDirection(second, p, r));
        // Half and half where both are equally near, the nearer alone from 60 % of the other's distance.
        color = mix(color, other, 0.5 * smoothstep(0.6, 1.0, d1 / max(d2, 1e-3)));
    }
    weight = 1.0 - smoothstep(kProbeFadeStart, kProbeFadeEnd, d1);
    return color;
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

// M17 windows F1b (decision 4): the warm glow of a light behind the pane at night (render::windowsAwake).
const vec3 kGlassGlowColor = vec3(1.0, 0.42, 0.12); // candle and hearth light through old glass (linear)
const float kGlassGlowStrength = 0.2;
const float kGlassStorey = 2.8; // m, the height of a storey: each storey of a house's side is one window row

// How much light burns behind the pane, 0..1. In a room of the world (indoor zones near the camera): its point
// lights behind the pane. Elsewhere by chance per window row (house = the model's origin, storey, side), lit
// when its number is below the share still awake; the brightness differs a little from row to row.
float glassLightBehind(vec3 worldPosition, vec3 n, vec3 origin, out float key)
{
    const vec3 behind = worldPosition - n * 0.4;
    const float storey = floor((worldPosition.y - origin.y) / kGlassStorey);
    const float side = floor(atan(n.z, n.x) / 1.5707963 + 0.5);
    key = glassHash(origin.xz * 0.731 + vec2(storey * 7.13 + side * 3.17, side * 1.91 + storey * 0.37));
    if (indoorAmount(behind) > 0.5)
    {
        vec3 sum = vec3(0.0);
        for (int i = 0; i < lightCount(); ++i)
        {
            const int index = lightIndex(i);
            const vec4 light = uLighting.pointPositionRadius[index];
            const vec3 d = light.xyz - worldPosition;
            if (dot(d, n) < -0.05) // behind the pane, in the room
            {
                sum += uLighting.pointColor[index].rgb * pointLightAttenuation(length(d), max(light.w, 4.0));
            }
        }
        return clamp(dot(sum, vec3(0.3, 0.5, 0.2)) * 4.0, 0.0, 1.0);
    }
    return key < uLighting.glassGlow.x ? 0.6 + 0.4 * glassHash(vec2(key * 91.7, storey + 3.1)) : 0.0;
}

// `pane` is the perturbed normal of glassPane: every small pane scatters the light differently, brightest
// where it bulges towards the eye; the light stands low in the room (a candle on the table), so the upper part
// of a window row is a little darker.
vec3 glassGlow(vec3 worldPosition, vec3 n, vec3 pane, vec3 origin)
{
    const float night = uLighting.glassGlow.y;
    if (night <= 0.0)
    {
        return vec3(0.0);
    }
    float key;
    const float light = glassLightBehind(worldPosition, n, origin, key);
    const float t = uLighting.glassGlow.z + key * 60.0;
    const float flicker = 0.93 + 0.07 * sin(t * 7.3) * sin(t * 2.9 + 1.0); // the flames move
    const float scatter = 0.45 + 0.55 * pow(max(dot(pane, n), 0.0), 12.0);
    const float height = mod(worldPosition.y - origin.y, kGlassStorey);
    const float low = 1.0 - 0.35 * smoothstep(0.8, 2.4, height);
    return kGlassGlowColor * (kGlassGlowStrength * light * flicker * night * scatter * low);
}

// The glass's colour: `albedo` is the dark pane behind (the room), `tint` the glass's own colour, `origin` the
// model's origin (the house, for the glow at night).
vec3 glassColor(vec3 worldPosition, vec3 cameraPosition, vec3 n, vec3 albedo, vec3 tint, int pattern, vec3 origin)
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
    // At night a light behind shines through the glass (F1b), the mirror on top as by day.
    const vec3 glow = glassGlow(worldPosition, n, pane, origin) * tint;
    // Stage B (medium, high): the probes show the street and the houses opposite, the sky where they are far.
    vec3 mirrored = glassSky(r);
    if (uLighting.skyZenith.w > 1.5)
    {
        float weight;
        const vec3 probe = probeReflection(worldPosition, r, weight);
        mirrored = mix(mirrored, probe, weight);
    }
    const vec3 glass = mix(behind + glow, mirrored * tint, fresnel) + glint * fresnel;
    const vec3 came = albedo * 0.5 * incomingLight(worldPosition, n); // lead: dull and dark
    return mix(glass, came, lead);
}
