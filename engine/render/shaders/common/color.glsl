// Shared colour helpers.

// Dusk palette until the sky system (M4) provides time-of-day colours.
const vec3 kDuskHorizon = vec3(0.32, 0.22, 0.20);
const vec3 kDuskZenith = vec3(0.06, 0.08, 0.14);
const vec3 kDuskGround = vec3(0.05, 0.045, 0.04);

// Interleaved gradient noise (Jimenez 2014): breaks up banding in smooth gradients.
float ditherNoise(vec2 pixel)
{
    return fract(52.9829189 * fract(dot(pixel, vec2(0.06711056, 0.00583715))));
}

// Exact sRGB transfer function (the hardware applies the inverse when sampling *_SRGB textures).
// Used until the tonemapping pass exists, which will then own the final encoding.
vec3 linearToSrgb(vec3 linear)
{
    const vec3 low = linear * 12.92;
    const vec3 high = 1.055 * pow(max(linear, vec3(0.0)), vec3(1.0 / 2.4)) - 0.055;
    return mix(high, low, lessThanEqual(linear, vec3(0.0031308)));
}
