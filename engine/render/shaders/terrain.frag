#version 450 core
// Terrain surface: lit and fogged like meshes; coloured by slope and height until the splat map
// (M4 part B) brings real materials.
#include "common/color.glsl"
#include "common/lighting.glsl"
#include "common/fog.glsl"

uniform vec3 uCameraPosition;
uniform vec2 uHeightRange; // minY, maxY

in vec3 vWorldPosition;
in vec3 vNormal;
out vec4 fragColor;

void main()
{
    const vec3 n = normalize(vNormal);
    // Linear albedos: grass on gentle slopes, earth on steeper ones, rock on cliffs.
    const vec3 grass = vec3(0.10, 0.16, 0.05);
    const vec3 earth = vec3(0.17, 0.12, 0.07);
    const vec3 rock = vec3(0.22, 0.21, 0.19);
    const float slope = 1.0 - n.y; // 0 = flat
    vec3 albedo = mix(grass, earth, smoothstep(0.08, 0.2, slope));
    albedo = mix(albedo, rock, smoothstep(0.3, 0.5, slope));
    // A little variation with height so plains do not look flat.
    const float h = (vWorldPosition.y - uHeightRange.x) / max(uHeightRange.y - uHeightRange.x, 1e-3);
    albedo *= mix(0.85, 1.1, h);

    vec3 color = albedo * incomingLight(vWorldPosition, n);
    color *= shadowDebugTint(vWorldPosition);
    color = applyFog(color, distance(vWorldPosition, uCameraPosition));
    fragColor = vec4(color, 1.0);
}
