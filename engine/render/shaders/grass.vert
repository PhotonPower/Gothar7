#version 450 core
// Grass (M17 G1, render::GrassRenderer): tufts from a buffer of 16-byte entries (foot position, packed bits),
// geometry from gl_VertexID - no vertex buffer. Stage 0: seven 3D blades per tuft; stage 1: two crossed cards
// with blades cut out in grass.frag. A tuft belongs to one stage (dithered over 2 m around the blade distance);
// on the last quarter of the radius tufts shrink into the ground and thin out. Wind as WindKind::Grass in
// common/wind.glsl: linear from the foot. With SHADOW only the position (sun shadows of the near tufts).
#include "common/wind.glsl"

layout(std430, binding = 6) readonly buffer Tufts
{
    uvec4 uTufts[]; // xyz: foot (float bits), w: packed (render::packGrassTuft)
};

uniform mat4 uViewProjection;
uniform vec3 uCameraPosition;
uniform float uRadius;        // fade out over its last quarter
uniform float uMaxDistance;   // nothing beyond (main pass: the radius; shadows: their distance)
uniform float uBladeDistance; // 3D blades up to here, cards beyond
uniform int uStage;           // 0 blades, 1 cards
uniform int uTileCount;
uniform int uTileStart[96]; // first instance of each listed tile
uniform int uTileBase[96];  // its first tuft in the buffer
uniform float uLayerColours[48]; // per terrain layer: foot rgb, tip rgb (linear)

out vec3 vWorldPosition;
out vec2 vCard; // stage 1: -1..1 across, 0..1 up
#ifndef SHADOW
out vec3 vNormal;
out vec3 vColour;
#endif

const float kMaxHeight = 1.2; // render::kGrassMaxHeight
const float kPi = 3.14159265;

float hash12(vec2 p)
{
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

void hide()
{
    vWorldPosition = vec3(0.0);
    vCard = vec2(0.0);
#ifndef SHADOW
    vNormal = vec3(0.0, 1.0, 0.0);
    vColour = vec3(0.0);
#endif
    gl_Position = vec4(2.0, 2.0, 2.0, 1.0); // outside the clip volume
}

// The wind's push at relative height w (0 foot .. 1 tip) of a blade `height` tall: WindKind::Grass.
vec3 windPush(vec3 foot, float height, float w)
{
    if (uWindState.z <= 0.0)
    {
        return vec3(0.0);
    }
    const float s = windAt(foot.xz);
    const float local = dot(foot.xz, vec2(0.37, 0.71)) + dot(foot.xz, vec2(1.7, 1.3));
    const vec3 dir = vec3(uWindState.x, 0.0, uWindState.y);
    const vec3 offset = dir * (0.25 * height * w * s * (0.7 + 0.3 * sin(uWindTime.x * 2.3 + local)));
    // Keep the blade's length roughly: the tip sinks a little as it bends.
    return offset - vec3(0.0, 0.5 * dot(offset, offset) / max(height, 0.05), 0.0);
}

void main()
{
    // The listed tile holding this instance (the last whose first instance is not after it).
    int lo = 0;
    int hi = uTileCount - 1;
    while (lo < hi)
    {
        const int mid = (lo + hi + 1) / 2;
        if (uTileStart[mid] <= gl_InstanceID)
        {
            lo = mid;
        }
        else
        {
            hi = mid - 1;
        }
    }
    const uvec4 tuft = uTufts[uTileBase[lo] + gl_InstanceID - uTileStart[lo]];
    const vec3 foot = uintBitsToFloat(tuft.xyz);
    const uint bits = tuft.w;
    const float yaw = float(bits & 63u) * (2.0 * kPi / 64.0);
    float height = float((bits >> 6) & 63u) / 63.0 * kMaxHeight;
    const int layer = int((bits >> 12) & 7u);
    const float tint = float((bits >> 15) & 7u);
    const int kind = int((bits >> 18) & 3u);
    const vec2 nxz = vec2(float((bits >> 20) & 63u), float((bits >> 26) & 63u)) / 63.0 * 2.0 - 1.0;

    const float distance = length(foot - uCameraPosition);
    if (distance > uMaxDistance)
    {
        hide();
        return;
    }
    // Fade: shrink into the ground and thin out over the last quarter.
    const float fade = smoothstep(0.75 * uRadius, uRadius, distance);
    if (hash12(foot.xz * 3.1) < fade * fade)
    {
        hide();
        return;
    }
    height *= 1.0 - fade;
    // Stage by a dither around the blade distance: each tuft is in exactly one.
    const float blend = clamp((distance - (uBladeDistance - 1.0)) * 0.5, 0.0, 1.0);
    const bool nearTuft = hash12(foot.xz * 7.7 + 1.3) >= blend;
    if ((uStage == 0) != nearTuft)
    {
        hide();
        return;
    }
#ifndef SHADOW
    vNormal = vec3(nxz.x, sqrt(max(1.0 - dot(nxz, nxz), 0.0)), nxz.y); // the ground's: no stripes
    const vec3 foot3 = vec3(uLayerColours[layer * 6], uLayerColours[layer * 6 + 1], uLayerColours[layer * 6 + 2]);
    const vec3 tip3 = vec3(uLayerColours[layer * 6 + 3], uLayerColours[layer * 6 + 4], uLayerColours[layer * 6 + 5]);
    const vec3 variation = vec3(0.85 + 0.04 * tint, 0.92 + 0.02 * tint, 1.1 - 0.05 * tint);
#endif
    float level; // 0 foot .. 1 tip, for the colour
    vec3 p;
    vCard = vec2(0.0);
    if (uStage == 0)
    {
        // Blade b, vertex k of 15: two quads (levels 0-1, 1-2) and the tip triangle (level 2 to the tip).
        const int b = gl_VertexID / 15;
        const int k = gl_VertexID % 15;
        const int quadSide[6] = int[6](-1, 1, 1, -1, 1, -1);
        const int quadLevel[6] = int[6](0, 0, 1, 0, 1, 1);
        int side;
        int lvl;
        if (k < 12)
        {
            side = quadSide[k % 6];
            lvl = quadLevel[k % 6] + k / 6;
        }
        else
        {
            side = k == 12 ? -1 : (k == 13 ? 1 : 0);
            lvl = k == 14 ? 3 : 2;
        }
        const float levels[4] = float[4](0.0, 0.4, 0.75, 1.0);
        const float t = levels[lvl];
        const float fb = float(b);
        const float h1 = hash12(foot.xz + vec2(fb * 1.37, 0.5));
        const float h2 = hash12(foot.xz + vec2(0.7, fb * 2.11));
        const float h3 = hash12(foot.xz * 1.9 + vec2(fb, fb * 0.3));
        const float angle = yaw + fb * 2.39996 + (h1 - 0.5) * 0.6; // golden angle around the tuft
        const vec3 out3 = vec3(cos(angle), 0.0, sin(angle));
        const vec3 across = vec3(-out3.z, 0.0, out3.x);
        const bool stem = kind != 0 && b < 2; // flowering tufts: two stems with a head
        float bladeHeight = height * (stem ? 1.1 : 0.55 + 0.45 * h2);
        const float spread = 0.03 + 0.1 * h1;
        const float lean = stem ? 0.08 : 0.15 + 0.3 * h3;
        float width = (stem ? 0.006 : 0.012 + 0.01 * h2) * (1.0 - 0.8 * t);
        bool head = false;
        if (stem && k >= 12)
        {
            // The head: a small flat triangle around the top.
            head = true;
        }
        const vec3 base = foot + out3 * spread;
        if (head)
        {
            const vec3 top = base + vec3(0.0, bladeHeight, 0.0) + out3 * (lean * bladeHeight);
            const vec3 corner = k == 12 ? -across : (k == 13 ? across : vec3(0.0, 1.0, 0.0));
            p = top + corner * 0.03 + windPush(foot, bladeHeight, 1.0);
            level = 1.0;
        }
        else
        {
            p = base + vec3(0.0, t * bladeHeight, 0.0) + out3 * (lean * bladeHeight * t * t) +
                across * (float(side) * width) + windPush(foot, bladeHeight, t);
            level = t;
        }
#ifndef SHADOW
        if (head)
        {
            const vec3 flowers[3] = vec3[3](vec3(0.8, 0.8, 0.75), vec3(0.85, 0.65, 0.05), vec3(0.15, 0.25, 0.8));
            vColour = flowers[kind - 1];
        }
        else
        {
            vColour = mix(foot3, tip3, level) * variation;
        }
#endif
    }
    else
    {
        // Two crossed cards; their blades are cut out in the fragment shader.
        const int q = gl_VertexID / 6;
        const vec2 corners[6] = vec2[6](vec2(-1.0, 0.0), vec2(1.0, 0.0), vec2(1.0, 1.0), vec2(-1.0, 0.0),
                                        vec2(1.0, 1.0), vec2(-1.0, 1.0));
        const vec2 c = corners[gl_VertexID % 6];
        const float a = yaw + float(q) * kPi * 0.5;
        const vec3 across = vec3(cos(a), 0.0, sin(a));
        const float halfWidth = 0.12 + 0.35 * height;
        p = foot + across * (c.x * halfWidth) + vec3(0.0, c.y * height, 0.0) + windPush(foot, height, c.y);
        vCard = vec2(c.x, c.y);
        level = c.y;
#ifndef SHADOW
        vColour = mix(foot3, tip3, level) * variation;
#endif
    }
    vWorldPosition = p;
    gl_Position = uViewProjection * vec4(p, 1.0);
}
