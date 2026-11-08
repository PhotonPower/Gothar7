#version 450 core
// Rain (M17 weather part R, render::RainRenderer): streaks and splashes from gl_InstanceID and the time, no vertex
// buffer. Positions are world-stable: each drop has its place in a box around the camera that wraps as the camera
// moves, so the rain does not travel with it. The rain map (depth from above) hides drops under roofs and puts the
// splashes on the top surfaces.

uniform mat4 uViewProjection;
uniform vec3 uCameraPosition;
uniform mat4 uMapViewProjection;
uniform int uHasMap;
uniform float uMapEyeY;    // height of the map's eye (its depth 0)
uniform float uMapHeight;  // metres from depth 0 to depth 1
uniform float uTime;
uniform vec2 uWind;        // m/s in world xz
uniform int uSplash;       // 0 streaks, 1 splashes
uniform float uRain;

layout(binding = 0) uniform sampler2D uRainMap; // Depth32F, 0 at the eye .. 1 below

out float vAlpha;
out vec2 vLocal;

const float kRadius = 20.0;       // half size of the box of streaks
const float kHeight = 24.0;       // its height
const float kSpeed = 8.0;         // falling speed m/s
const float kLength = 0.45;       // streak length m
const float kSplashRadius = 10.0; // half size of the box of splashes
const float kSplashLife = 0.45;   // seconds

float hash(float n)
{
    return fract(sin(n) * 43758.5453);
}

vec3 hash3(float n)
{
    return vec3(hash(n * 1.17 + 0.3), hash(n * 2.71 + 1.7), hash(n * 3.13 + 5.1));
}

vec2 wrapAround(vec2 p, vec2 centre, float radius)
{
    return mod(p - (centre - radius), 2.0 * radius) + centre - radius;
}

// The map's depth over p and p's own depth in it; false outside the map.
bool mapAt(vec3 p, out float surface, out float own)
{
    const vec4 c = uMapViewProjection * vec4(p, 1.0);
    const vec2 uv = c.xy / c.w * 0.5 + 0.5;
    own = c.z / c.w;
    surface = 1.0;
    if (any(lessThan(uv, vec2(0.0))) || any(greaterThan(uv, vec2(1.0))))
    {
        return false;
    }
    surface = texture(uRainMap, uv).r;
    return true;
}

void hide()
{
    vAlpha = 0.0;
    gl_Position = vec4(2.0, 2.0, 2.0, 1.0); // outside the clip volume
}

void main()
{
    const vec2 corners[6] = vec2[](vec2(-1.0, 0.0), vec2(1.0, 0.0), vec2(1.0, 1.0), vec2(-1.0, 0.0), vec2(1.0, 1.0),
                                   vec2(-1.0, 1.0));
    const vec2 corner = corners[gl_VertexID];
    vLocal = corner;
    const float id = float(gl_InstanceID);
    if (uSplash == 0)
    {
        const vec3 h = hash3(id);
        // Drifting with the wind while falling; wrapped into the box around the camera.
        const vec2 xz = wrapAround(h.xy * 2.0 * kRadius + uWind * uTime, uCameraPosition.xz, kRadius);
        const float y = mod(h.z * kHeight - uTime * kSpeed - (uCameraPosition.y - 0.5 * kHeight), kHeight) +
                        uCameraPosition.y - 0.5 * kHeight;
        const vec3 head = vec3(xz.x, y, xz.y);
        if (uHasMap == 1)
        {
            float surface;
            float own;
            if (mapAt(head, surface, own) && own > surface + 0.0005)
            {
                hide(); // under a roof (or below the ground)
                return;
            }
        }
        const vec3 direction = normalize(vec3(uWind.x, -kSpeed, uWind.y));
        const vec3 tail = head - direction * kLength;
        const vec3 toCamera = head - uCameraPosition;
        const float distance = length(toCamera);
        const vec3 side = normalize(cross(direction, toCamera)) * (0.004 + 0.0012 * distance);
        const vec3 p = mix(head, tail, corner.y) + side * corner.x;
        vAlpha = uRain * smoothstep(0.3, 1.5, distance) * (1.0 - smoothstep(0.6 * kRadius, kRadius, distance));
        gl_Position = uViewProjection * vec4(p, 1.0);
        return;
    }
    // Splashes: each lives kSplashLife, then appears elsewhere; on the top surface from the rain map.
    const float cycle = floor(uTime / kSplashLife + hash(id * 0.37));
    const float age = fract(uTime / kSplashLife + hash(id * 0.37));
    const vec3 h = hash3(id * 1.91 + cycle * 7.31);
    const vec2 xz = wrapAround(h.xy * 2.0 * kSplashRadius, uCameraPosition.xz, kSplashRadius);
    float surface;
    float own;
    if (!mapAt(vec3(xz.x, uCameraPosition.y, xz.y), surface, own) || surface >= 0.9999)
    {
        hide();
        return;
    }
    const float y = uMapEyeY - surface * uMapHeight + 0.02;
    const float radius = 0.02 + 0.1 * age;
    const vec2 c = vec2(corner.x, corner.y * 2.0 - 1.0); // -1..1 in both
    vLocal = c;
    const vec3 p = vec3(xz.x + c.x * radius, y, xz.y + c.y * radius);
    vAlpha = uRain * (1.0 - age) * (1.0 - smoothstep(0.6 * kSplashRadius, kSplashRadius, length(p - uCameraPosition)));
    gl_Position = uViewProjection * vec4(p, 1.0);
}
