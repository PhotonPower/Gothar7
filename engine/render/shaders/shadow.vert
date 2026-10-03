#version 450 core
// Depth-only pass into a shadow cascade (uViewProjection = the cascade's light matrix).
// MULTI_DRAW: the model matrix comes from the draw's DrawData.
#include "common/draws.glsl"

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
    gl_Position = uViewProjection * model * vec4(aPosition, 1.0);
}
