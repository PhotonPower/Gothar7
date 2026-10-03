#version 450 core
// Static mesh (asset::Vertex layout). MULTI_DRAW: the model matrix comes from the draw's DrawData.
#include "common/draws.glsl"

layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUv;
layout(location = 3) in vec4 aTangent; // xyz + handedness; zero if the mesh has none

uniform mat4 uViewProjection;
#ifdef MULTI_DRAW
layout(location = 4) in uint aDrawIndex;
flat out uint vDrawIndex;
#else
uniform mat4 uModel;
#endif

out vec3 vWorldPosition;
out vec3 vNormal;
out vec4 vTangent;
out vec2 vUv;

void main()
{
#ifdef MULTI_DRAW
    const mat4 model = uDraws[aDrawIndex].model;
    const mat3 normalMatrix = mat3(uDraws[aDrawIndex].normalMatrix);
    vDrawIndex = aDrawIndex;
#else
    const mat4 model = uModel;
    const mat3 normalMatrix = mat3(transpose(inverse(model)));
#endif
    vNormal = normalMatrix * aNormal;
    vTangent = vec4(mat3(model) * aTangent.xyz, aTangent.w);
    vUv = aUv;
    const vec4 world = model * vec4(aPosition, 1.0);
    vWorldPosition = world.xyz;
    gl_Position = uViewProjection * world;
}
