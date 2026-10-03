#version 450 core
// Static mesh (asset::Vertex layout). MULTI_DRAW: the model matrix comes from the draw's DrawData.
// SKINNED: positions, normals and tangents first go through the vertex's bones (common/skinning.glsl).
#include "common/draws.glsl"
#include "common/skinning.glsl"

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
#ifdef SKINNED
    const mat4 skin = skinMatrix();
    const vec3 position = (skin * vec4(aPosition, 1.0)).xyz;
    const vec3 normal = mat3(skin) * aNormal; // bones are rigid: no inverse transpose needed
    const vec3 tangent = mat3(skin) * aTangent.xyz;
#else
    const vec3 position = aPosition;
    const vec3 normal = aNormal;
    const vec3 tangent = aTangent.xyz;
#endif
    vNormal = normalMatrix * normal;
    vTangent = vec4(mat3(model) * tangent, aTangent.w);
    vUv = aUv;
    const vec4 world = model * vec4(position, 1.0);
    vWorldPosition = world.xyz;
    gl_Position = uViewProjection * world;
}
