// Wind (M17 W, render::Wind): bends the vegetation and swings the guild signs. The same formulas as
// render::windBent on the CPU. uWindBend / uWindPivot per material (render::WindBend), the Wind block
// once per frame (render::GpuWind). Static meshes only (no wind on figures, decision 7).
#ifndef SKINNED
layout(std140, binding = 4) uniform Wind
{
    vec4 uWindState; // direction xz, strength, gusts
    vec4 uWindTime;  // x: seconds
};
uniform vec4 uWindBend;  // kind (asset::WindKind: 0 none, 1 trunk, 2 leaves, 3 grass, 4 cloth, 5 hanging), strength, foot, height
uniform vec4 uWindPivot; // xyz: pivot in model space (hanging); w: hinge (0 free, 1 model X, 2 model Z)

const float kWindGustScale = 0.035;
const float kWindGustSpeed = 6.0;

float windHash(vec2 cell)
{
    return fract(sin(dot(cell, vec2(127.1, 311.7))) * 43758.5453);
}

float windNoise(vec2 p)
{
    const vec2 cell = floor(p);
    const vec2 f = p - cell;
    const vec2 u = f * f * (3.0 - 2.0 * f);
    const float a = windHash(cell);
    const float b = windHash(cell + vec2(1.0, 0.0));
    const float c = windHash(cell + vec2(0.0, 1.0));
    const float d = windHash(cell + vec2(1.0, 1.0));
    return mix(mix(a, b, u.x), mix(c, d, u.x), u.y);
}

float windAt(vec2 xz)
{
    const float gust =
        windNoise(xz * kWindGustScale - uWindState.xy * (uWindTime.x * kWindGustSpeed * kWindGustScale));
    return clamp(uWindState.z * (1.0 + uWindState.w * 1.5 * (2.0 * gust - 1.0)), 0.0, 1.0);
}

// The bent world position of a vertex at `modelPosition` (world = model * modelPosition); hanging materials
// also turn the normal and the tangent.
vec3 windBent(vec3 modelPosition, mat4 model, vec3 world, inout vec3 normal, inout vec3 tangent)
{
    const int kind = int(uWindBend.x + 0.5);
    if (kind == 0 || uWindState.z <= 0.0)
    {
        return world;
    }
    const vec3 origin = model[3].xyz;
    const float s = windAt(origin.xz) * uWindBend.y;
    const float phase = dot(origin.xz, vec2(0.37, 0.71));
    const float t = uWindTime.x;
    const vec3 dir = vec3(uWindState.x, 0.0, uWindState.y);
    if (kind == 5) // hanging: rigid about the pivot
    {
        float angle = radians(30.0) * s * (0.8 + 0.2 * sin(t * 1.7 + phase));
        const vec3 pivot = (model * vec4(uWindPivot.xyz, 1.0)).xyz;
        const vec3 up = vec3(0.0, 1.0, 0.0);
        vec3 axis = cross(dir, up); // free: the bottom swings downwind
        const int hinge = int(uWindPivot.w + 0.5);
        if (hinge != 0) // hinged: only the wind across the hinge swings it
        {
            const vec3 h = mat3(model) * (hinge == 1 ? vec3(1.0, 0.0, 0.0) : vec3(0.0, 0.0, 1.0));
            axis = normalize(vec3(h.x, 0.0, h.z));
            angle *= dot(dir, cross(up, axis));
        }
        const float c = cos(angle);
        const float sn = sin(angle);
        const vec3 r = world - pivot;
        normal = normal * c + cross(axis, normal) * sn + axis * dot(axis, normal) * (1.0 - c);
        tangent = tangent * c + cross(axis, tangent) * sn + axis * dot(axis, tangent) * (1.0 - c);
        return pivot + r * c + cross(axis, r) * sn + axis * dot(axis, r) * (1.0 - c);
    }
    const float heightWorld = uWindBend.w * length(model[1].xyz);
    float w = clamp((modelPosition.y - uWindBend.z) / uWindBend.w, 0.0, 1.0);
    vec3 offset;
    if (kind <= 2) // trunk, leaves: quadratic
    {
        w *= w;
        offset = dir * (0.04 * heightWorld * w * s * (0.75 + 0.25 * sin(t * 1.1 + phase)));
        if (kind == 2)
        {
            const float flutter = sin(t * 7.0 + dot(world, vec3(3.1, 2.3, 2.7)));
            offset += vec3(-dir.z, 0.6, dir.x) * (0.03 * w * s * flutter);
        }
    }
    else // grass, cloth: linear
    {
        const float local = phase + dot(world.xz, vec2(1.7, 1.3));
        const float amount = kind == 3 ? 0.25 : 0.15;
        offset = dir * (amount * heightWorld * w * s * (0.7 + 0.3 * sin(t * 2.3 + local)));
        if (kind == 4)
        {
            offset += vec3(-dir.z, 0.0, dir.x) * (0.05 * heightWorld * w * s * sin(t * 9.0 + local * 3.0));
        }
    }
    return world + offset;
}
#endif
