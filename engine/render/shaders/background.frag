#version 450 core
// Background gradient by view direction (placeholder for the sky, M4): the horizon stays put when
// the camera looks up or down. Its horizon colour is the fog colour, so distant geometry fades
// into the sky; below the horizon lies only infinitely distant, fully fogged ground, so it keeps
// the fog colour (no dark band beyond the edge of the world). Output is linear HDR.
#include "common/color.glsl"

uniform mat4 uInverseViewProjection;
uniform vec3 uCameraPosition;
uniform vec3 uHorizonColor; // linear, = Environment::fogColor

in vec2 vNdc;
out vec4 fragColor;

void main()
{
    const vec4 world = uInverseViewProjection * vec4(vNdc, 0.5, 1.0);
    const vec3 direction = normalize(world.xyz / world.w - uCameraPosition);

    const vec3 color = mix(uHorizonColor, srgbToLinear(kDuskZenith), smoothstep(0.0, 0.6, direction.y));
    fragColor = vec4(color, 1.0);
}
