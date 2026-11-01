#version 450 core
// Game UI (ui::Canvas, M14): images (sRGB textures, read back to display values) and text from a
// multi-channel signed distance field (median of the three channels, edge at 0.5; ADR 0020).

layout(binding = 0) uniform sampler2D uTexture;

in vec2 vUv;
in vec4 vColor;
in float vPixelRange;
in float vOutline;
in vec4 vOutlineColor;
out vec4 fragColor;

float median(vec3 v)
{
    return max(min(v.r, v.g), min(max(v.r, v.g), v.b));
}

vec3 linearToDisplay(vec3 c)
{
    // sRGB transfer function: the texture unit decodes sRGB images to linear, the window wants display values.
    return mix(c * 12.92, 1.055 * pow(c, vec3(1.0 / 2.4)) - 0.055, step(vec3(0.0031308), c));
}

void main()
{
    vec4 texel = texture(uTexture, vUv);
    if (vPixelRange < 0.0)
    {
        fragColor = vColor * texel; // a texture of display values (rendered item cells)
        return;
    }
    if (vPixelRange <= 0.0)
    {
        fragColor = vColor * vec4(linearToDisplay(texel.rgb), texel.a);
        return;
    }
    // Signed distance in screen pixels: > 0 inside the glyph.
    float distance = vPixelRange * (median(texel.rgb) - 0.5);
    float fill = clamp(distance + 0.5, 0.0, 1.0);
    if (vOutline <= 0.0)
    {
        fragColor = vec4(vColor.rgb, vColor.a * fill);
        return;
    }
    float edge = clamp(distance + vOutline + 0.5, 0.0, 1.0);
    fragColor = vec4(mix(vOutlineColor.rgb, vColor.rgb, fill), max(vColor.a * fill, vOutlineColor.a * edge));
}
