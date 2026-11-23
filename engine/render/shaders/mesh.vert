#version 450 core
// Static mesh (asset::Vertex layout). MULTI_DRAW: the model matrix comes from the draw's DrawData.
// SKINNED: positions, normals and tangents first go through the vertex's bones (common/skinning.glsl).
#include "common/draws.glsl"
#include "common/skinning.glsl"
#include "common/wind.glsl"

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
#ifdef SKINNED
// M17 part G: the bones whose weights count as the glowing hand (bit j of word j / 32; render::FigureGlow).
uniform int uGlowBones[4];
out float vGlowHand;

float glowBone(uint joint)
{
    return ((uint(uGlowBones[joint >> 5u]) >> (joint & 31u)) & 1u) != 0u ? 1.0 : 0.0;
}
#endif

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
    // Forearm and hand only: the elbow's blend towards the upper arm fades out (decision 9).
    vGlowHand = smoothstep(0.55, 0.95, aWeights.x * glowBone(aJoints.x) + aWeights.y * glowBone(aJoints.y) +
                                           aWeights.z * glowBone(aJoints.z) + aWeights.w * glowBone(aJoints.w));
    const vec3 position = (skin * vec4(aPosition, 1.0)).xyz;
    const vec3 normal = mat3(skin) * aNormal; // bones are rigid: no inverse transpose needed
    const vec3 tangent = mat3(skin) * aTangent.xyz;
#else
    const vec3 position = aPosition;
    const vec3 normal = aNormal;
    const vec3 tangent = aTangent.xyz;
#endif
    vec3 worldNormal = normalMatrix * normal;
    vec3 worldTangent = mat3(model) * tangent;
    vec3 world = (model * vec4(position, 1.0)).xyz;
#ifndef SKINNED
    world = windBent(position, model, world, worldNormal, worldTangent); // M17 W
#endif
    vNormal = worldNormal;
    vTangent = vec4(worldTangent, aTangent.w);
    vUv = aUv;
    vWorldPosition = world;
    gl_Position = uViewProjection * vec4(world, 1.0);
}
