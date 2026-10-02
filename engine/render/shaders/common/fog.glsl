// Distance fog (render.md): no fog before fogColorStart.w, then exponential-squared. Needs the
// Lighting block from common/lighting.glsl. Same formula as render::fogFactor.

float fogFactor(float distance)
{
    const float x = max(distance - uLighting.fogColorStart.w, 0.0) * uLighting.fogParams.x;
    return 1.0 - exp(-x * x);
}

vec3 applyFog(vec3 color, float distance)
{
    return mix(color, uLighting.fogColorStart.rgb, fogFactor(distance));
}
