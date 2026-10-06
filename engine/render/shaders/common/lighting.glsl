// Stylised forward lighting (render.md): Lambert sun, hemisphere ambient, up to 8 point lights per
// object (selected on the CPU). Layout must match render::GpuLighting (std140).

const int kMaxPointLights = 256;
const int kMaxIndoorVolumes = 32;
const int kMaxLightsPerObject = 8;

layout(std140, binding = 0) uniform Lighting
{
    vec4 sunDirection;  // xyz, towards the sun
    vec4 sunColor;      // rgb * intensity
    vec4 ambientSky;
    vec4 ambientGround;
    ivec4 counts;       // x = point lights this frame
    mat4 cascadeViewProjection[4];
    vec4 cascadeSplits;     // view depth where cascade i ends
    vec4 cascadeRects[4];   // atlas tile: uv offset xy, size zw
    vec4 cascadeTexelSize;  // world size of a shadow texel per cascade
    vec4 shadowParams;      // x enabled, y cascades, z distance, w normal offset (texels)
    vec4 shadowExtra;       // x debug colours, y atlas texel size (uv)
    vec4 cameraPosition;
    vec4 cameraForward;
    vec4 fogColorStart;     // rgb linear, w start distance
    vec4 fogParams;         // x density
    vec4 indoorParams;      // x rooms, y ambient factor inside, z edge (m)
    vec4 indoorBoxes[2 * kMaxIndoorVolumes]; // per room: centre + cos yaw, half extents + sin yaw
    vec4 pointPositionRadius[kMaxPointLights];
    vec4 pointColor[kMaxPointLights];
} uLighting;

#ifdef MULTI_DRAW
#include "common/draws.glsl"
flat in uint vDrawIndex;
int lightCount() { return uDraws[vDrawIndex].counts.x; }
int lightIndex(int i) { return i < 4 ? uDraws[vDrawIndex].lights0[i] : uDraws[vDrawIndex].lights1[i - 4]; }
#else
uniform int uLightIndices[kMaxLightsPerObject]; // indices into the point lights for this draw
uniform int uLightCount;
int lightCount() { return uLightCount; }
int lightIndex(int i) { return uLightIndices[i]; }
#endif

// saturate(1 - (d/r)^4)^2 / (d^2 + 1): 1 at the light, exactly 0 at the radius (render::pointLightAttenuation).
float pointLightAttenuation(float distance, float radius)
{
    const float ratio = distance / radius;
    const float window = clamp(1.0 - ratio * ratio * ratio * ratio, 0.0, 1.0);
    return window * window / (distance * distance + 1.0);
}

layout(binding = 3) uniform sampler2DShadow uShadowAtlas; // cascades in 2x2 tiles, compare LessEqual

int shadowCascade(vec3 worldPosition)
{
    const float viewDepth = dot(worldPosition - uLighting.cameraPosition.xyz, uLighting.cameraForward.xyz);
    const int count = int(uLighting.shadowParams.y);
    for (int i = 0; i < count; ++i)
    {
        if (viewDepth < uLighting.cascadeSplits[i])
        {
            return i;
        }
    }
    return -1; // beyond the shadow distance
}

// Sun visibility 0 (shadowed) .. 1 (lit): cascade by view depth, normal offset against acne,
// 3x3 PCF on top of the hardware 2x2 comparison, faded out over the last 10 % of the distance.
float sunShadow(vec3 worldPosition, vec3 n)
{
    if (uLighting.shadowParams.x < 0.5)
    {
        return 1.0;
    }
    const int cascade = shadowCascade(worldPosition);
    if (cascade < 0)
    {
        return 1.0;
    }
    const vec3 offsetPosition = worldPosition + n * uLighting.cascadeTexelSize[cascade] * uLighting.shadowParams.w;
    const vec4 clip = uLighting.cascadeViewProjection[cascade] * vec4(offsetPosition, 1.0);
    const vec3 ndc = clip.xyz / clip.w;
    const vec2 uv = ndc.xy * 0.5 + 0.5;
    if (any(lessThan(uv, vec2(0.0))) || any(greaterThan(uv, vec2(1.0))) || ndc.z > 1.0)
    {
        return 1.0;
    }
    const vec4 rect = uLighting.cascadeRects[cascade];
    const float texel = uLighting.shadowExtra.y;
    // Keep the kernel inside the tile so neighbouring cascades never bleed in.
    const vec2 atlasUv = clamp(rect.xy + uv * rect.zw, rect.xy + 1.5 * texel, rect.xy + rect.zw - 1.5 * texel);
    float lit = 0.0;
    for (int y = -1; y <= 1; ++y)
    {
        for (int x = -1; x <= 1; ++x)
        {
            lit += texture(uShadowAtlas, vec3(atlasUv + vec2(x, y) * texel, ndc.z));
        }
    }
    lit /= 9.0;
    const float viewDepth = dot(worldPosition - uLighting.cameraPosition.xyz, uLighting.cameraForward.xyz);
    const float fade = smoothstep(uLighting.shadowParams.z * 0.9, uLighting.shadowParams.z, viewDepth);
    return mix(lit, 1.0, fade);
}

// Debug colour per cascade (shadow_debug), white outside the shadow distance.
vec3 shadowDebugTint(vec3 worldPosition)
{
    if (uLighting.shadowExtra.x < 0.5 || uLighting.shadowParams.x < 0.5)
    {
        return vec3(1.0);
    }
    const vec3 colours[4] = vec3[4](vec3(1.0, 0.4, 0.4), vec3(0.4, 1.0, 0.4), vec3(0.4, 0.4, 1.0), vec3(1.0, 1.0, 0.4));
    const int cascade = shadowCascade(worldPosition);
    return cascade < 0 ? vec3(1.0) : colours[cascade];
}

// How much a point is inside the rooms (world.md "zones", indoor), 0..1: 1 in a box (5 cm tolerance: its
// inner surfaces), fading to 0 at the edge beyond it (through the wall). As render::indoorAmount.
float indoorAmount(vec3 p)
{
    float amount = 0.0;
    const int rooms = int(uLighting.indoorParams.x);
    for (int i = 0; i < rooms; ++i)
    {
        const vec4 a = uLighting.indoorBoxes[2 * i];
        const vec4 b = uLighting.indoorBoxes[2 * i + 1];
        const vec3 d = p - a.xyz;
        const vec3 local = vec3(d.x * a.w - d.z * b.w, d.y, d.x * b.w + d.z * a.w);
        const vec3 outside = max(abs(local) - b.xyz - vec3(0.05), vec3(0.0));
        const float beyond = max(outside.x, max(outside.y, outside.z));
        amount = max(amount, clamp(1.0 - beyond / max(uLighting.indoorParams.z, 1e-3), 0.0, 1.0));
    }
    return amount;
}

// Incoming light (to be multiplied by the albedo) at a surface point with normal n.
vec3 incomingLight(vec3 worldPosition, vec3 n)
{
    vec3 light = mix(uLighting.ambientGround.rgb, uLighting.ambientSky.rgb, n.y * 0.5 + 0.5);
    if (uLighting.indoorParams.x > 0.5)
    {
        light *= mix(1.0, uLighting.indoorParams.y, indoorAmount(worldPosition)); // rooms: the indoor ambient
    }
    light += uLighting.sunColor.rgb * max(dot(n, uLighting.sunDirection.xyz), 0.0) * sunShadow(worldPosition, n);
    for (int i = 0; i < lightCount(); ++i)
    {
        const int index = lightIndex(i);
        const vec4 positionRadius = uLighting.pointPositionRadius[index];
        const vec3 toLight = positionRadius.xyz - worldPosition;
        const float distance = length(toLight);
        const float attenuation = pointLightAttenuation(distance, positionRadius.w);
        light += uLighting.pointColor[index].rgb * attenuation * max(dot(n, toLight / max(distance, 1e-4)), 0.0);
    }
    return light;
}
