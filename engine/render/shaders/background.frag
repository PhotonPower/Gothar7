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

in vec2 vNdc;
out vec4 fragColor;

float hash(vec3 p)
{
    p = fract(p * 0.3183099 + 0.1);
    p *= 17.0;
    return fract(p.x * p.y * p.z * (p.x + p.y + p.z));
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
    }
    fragColor = vec4(color, 1.0);
}
