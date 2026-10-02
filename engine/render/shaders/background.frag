#version 450 core
// Background gradient by view direction (placeholder for the sky, M4): the horizon stays put when
// the camera looks up or down.
#include "common/color.glsl"

uniform mat4 uInverseViewProjection;
uniform vec3 uCameraPosition;

in vec2 vNdc;
out vec4 fragColor;

void main()
{
    const vec4 world = uInverseViewProjection * vec4(vNdc, 0.5, 1.0);
    const vec3 direction = normalize(world.xyz / world.w - uCameraPosition);

    vec3 color;
    if (direction.y >= 0.0)
    {
        color = mix(kDuskHorizon, kDuskZenith, smoothstep(0.0, 0.6, direction.y));
    }
    else
    {
        color = mix(kDuskHorizon, kDuskGround, smoothstep(0.0, 0.08, -direction.y));
    }
    color += (ditherNoise(gl_FragCoord.xy) - 0.5) / 255.0;
    fragColor = vec4(color, 1.0);
}
