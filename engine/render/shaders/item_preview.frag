#version 450 core
// Inventory cells (ui::ItemPreviews, M14 part D): the item's linear HDR render (twice the tile size) into its
// atlas tile - exposure, ACES, sRGB encoding like the post pass, and the coverage as alpha (the cell's frame
// shows through).
#include "common/color.glsl"

layout(binding = 0) uniform sampler2D uScene; // RGBA16F, linear, alpha = coverage

uniform float uExposure;

in vec2 vUv;
out vec4 fragColor;

void main()
{
    const vec4 hdr = texture(uScene, vUv); // the linear filter averages the 2 x 2 samples
    const float coverage = clamp(hdr.a, 0.0, 1.0);
    if (coverage <= 0.0)
    {
        fragColor = vec4(0.0);
        return;
    }
    // The background was cleared to black: undo its weight at the edges before the curve.
    vec3 x = max(hdr.rgb / coverage, vec3(0.0)) * uExposure;
    x = clamp((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14), 0.0, 1.0);
    fragColor = vec4(linearToSrgb(x), coverage);
}
