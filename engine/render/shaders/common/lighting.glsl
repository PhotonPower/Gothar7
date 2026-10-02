// Stylised forward lighting (render.md): Lambert sun, hemisphere ambient, up to 8 point lights per
// object (selected on the CPU). Layout must match render::GpuLighting (std140).

const int kMaxPointLights = 256;
const int kMaxLightsPerObject = 8;

layout(std140, binding = 0) uniform Lighting
{
    vec4 sunDirection;  // xyz, towards the sun
    vec4 sunColor;      // rgb * intensity
    vec4 ambientSky;
    vec4 ambientGround;
    ivec4 counts;       // x = point lights this frame
    vec4 pointPositionRadius[kMaxPointLights];
    vec4 pointColor[kMaxPointLights];
} uLighting;

uniform int uLightIndices[kMaxLightsPerObject]; // indices into the point lights for this draw
uniform int uLightCount;

// saturate(1 - (d/r)^4)^2 / (d^2 + 1): 1 at the light, exactly 0 at the radius (render::pointLightAttenuation).
float pointLightAttenuation(float distance, float radius)
{
    const float ratio = distance / radius;
    const float window = clamp(1.0 - ratio * ratio * ratio * ratio, 0.0, 1.0);
    return window * window / (distance * distance + 1.0);
}

// Incoming light (to be multiplied by the albedo) at a surface point with normal n.
vec3 incomingLight(vec3 worldPosition, vec3 n)
{
    vec3 light = mix(uLighting.ambientGround.rgb, uLighting.ambientSky.rgb, n.y * 0.5 + 0.5);
    light += uLighting.sunColor.rgb * max(dot(n, uLighting.sunDirection.xyz), 0.0);
    for (int i = 0; i < uLightCount; ++i)
    {
        const int index = uLightIndices[i];
        const vec4 positionRadius = uLighting.pointPositionRadius[index];
        const vec3 toLight = positionRadius.xyz - worldPosition;
        const float distance = length(toLight);
        const float attenuation = pointLightAttenuation(distance, positionRadius.w);
        light += uLighting.pointColor[index].rgb * attenuation * max(dot(n, toLight / max(distance, 1e-4)), 0.0);
    }
    return light;
}
