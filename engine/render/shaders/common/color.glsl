// Shared colour helpers.

// Dusk palette until the sky system (M4) provides time-of-day colours.
const vec3 kDuskHorizon = vec3(0.32, 0.22, 0.20);
const vec3 kDuskZenith = vec3(0.06, 0.08, 0.14);

// Interleaved gradient noise (Jimenez 2014): breaks up banding in smooth gradients.
float ditherNoise(vec2 pixel)
{
    return fract(52.9829189 * fract(dot(pixel, vec2(0.06711056, 0.00583715))));
}
