#version 450 core
// Sky by view direction (M4 day and night): from the horizon (= fog colour, so distant geometry
// fades into it) to the zenith, sun and moon discs, procedural stars. Below the horizon lies only
// infinitely distant, fully fogged ground: it keeps the horizon colour. Output is linear HDR.
#include "common/color.glsl"

uniform mat4 uInverseViewProjection;
uniform vec3 uCameraPosition;
uniform vec3 uHorizonColor; // linear, = Environment::fogColor
uniform vec3 uZenithColor;
uniform vec3 uSunDirection; // towards the sun
uniform vec3 uSunColor;
uniform vec3 uMoonDirection;
uniform float uMoon;  // 0 .. 1
uniform float uStars; // 0 .. 1
uniform vec4 uBolt;   // M17 G: lightning bolt - x azimuth (radians about +Y), y brightness 0..1, z seed, w top (sine of
                      // its elevation); brightness 0: none

in vec2 vNdc;
out vec4 fragColor;

float hash(vec3 p)
{
    p = fract(p * 0.3183099 + 0.1);
    p *= 17.0;
    return fract(p.x * p.y * p.z * (p.x + p.y + p.z));
}

float boltNoise(float x)
{
    const float i = floor(x);
    const float f = fract(x);
    return mix(hash(vec3(i, uBolt.z, 1.0)), hash(vec3(i + 1.0, uBolt.z, 1.0)), f) * 2.0 - 1.0;
}

// A jagged bright streak from the horizon up to uBolt.w in the bolt's direction, with a faint glow around it
// (M17 G; the light on the world comes from the engine, not from this).
vec3 lightningBolt(vec3 direction)
{
    const vec3 toward = vec3(sin(uBolt.x), 0.0, -cos(uBolt.x)); // azimuth 0 = -Z, positive turns towards +X
    const vec2 horizontal = normalize(direction.xz + vec2(1e-6));
    if (dot(horizontal, toward.xz) < 0.0 || direction.y > uBolt.w)
    {
        return vec3(0.0);
    }
    const float across = horizontal.x * -toward.z + horizontal.y * toward.x; // sine of the angle beside the bolt
    const float height = direction.y / max(uBolt.w, 1e-3);       // 0 horizon .. 1 top
    const float path = 0.025 * boltNoise(height * 6.0) + 0.008 * boltNoise(height * 23.0) + 0.003 * boltNoise(height * 71.0);
    const float d = abs(across - path * (0.3 + height));
    const float core = smoothstep(0.004, 0.0, d);
    const float glow = exp(-d * 40.0) * 0.3;
    return vec3(0.85, 0.9, 1.0) * (core * 12.0 + glow) * uBolt.y * smoothstep(1.0, 0.85, height);
}

void main()
{
    const vec4 world = uInverseViewProjection * vec4(vNdc, 0.5, 1.0);
    const vec3 direction = normalize(world.xyz / world.w - uCameraPosition);
    const float up = direction.y;

    vec3 color = mix(uHorizonColor, uZenithColor, smoothstep(0.0, 0.6, up));
    if (up > 0.0)
    {
        // Stars: a fixed pattern on the sky (cells of the direction), fading in towards the zenith.
        if (uStars > 0.0)
        {
            const vec3 cell = floor(direction * 400.0);
            const float star = step(0.9975, hash(cell)) * (0.4 + 0.6 * hash(cell + 7.0));
            color += vec3(star * uStars * smoothstep(0.02, 0.25, up) * 1.5);
        }
        // Sun: a bright disc with a soft glow; the moon: a pale disc.
        const float sun = dot(direction, normalize(uSunDirection));
        color += uSunColor * (smoothstep(0.99955, 0.9998, sun) * 40.0 + pow(max(sun, 0.0), 48.0) * 0.6);
        const float moon = dot(direction, normalize(uMoonDirection));
        color += vec3(0.75, 0.8, 0.95) * smoothstep(0.99935, 0.9996, moon) * uMoon * 2.0;
        if (uBolt.y > 0.0)
        {
            color += lightningBolt(direction);
        }
    }
    fragColor = vec4(color, 1.0);
}
