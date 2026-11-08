#version 450 core
// Depth-only pass into a shadow cascade (uViewProjection = the cascade's light matrix).
// MULTI_DRAW: the model matrix comes from the draw's DrawData.
#include "common/draws.glsl"
#include "common/skinning.glsl"
#include "common/wind.glsl"

layout(location = 0) in vec3 aPosition;
layout(location = 2) in vec2 aUv;

uniform mat4 uViewProjection;
#ifdef MULTI_DRAW
layout(location = 4) in uint aDrawIndex;
#else
uniform mat4 uModel;
#endif

out vec2 vUv;

void main()
{
#ifdef MULTI_DRAW
    const mat4 model = uDraws[aDrawIndex].model;
#else
    const mat4 model = uModel;
#endif
    vUv = aUv;
#ifdef SKINNED
    const vec4 position = skinMatrix() * vec4(aPosition, 1.0);
    gl_Position = uViewProjection * model * position;
#else
    // M17 W: bent like in mesh.vert, so shadows and the depth prepass follow the wind.
    vec3 normal = vec3(0.0, 1.0, 0.0);
    vec3 tangent = vec3(1.0, 0.0, 0.0);
    const vec3 world = windBent(aPosition, model, (model * vec4(aPosition, 1.0)).xyz, normal, tangent);
    gl_Position = uViewProjection * vec4(world, 1.0);
#endif
}
