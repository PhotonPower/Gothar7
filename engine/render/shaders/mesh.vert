#version 450 core
// Static mesh (asset::Vertex layout).

layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUv;
layout(location = 3) in vec4 aTangent; // xyz + handedness; zero if the mesh has none

uniform mat4 uViewProjection;
uniform mat4 uModel;

out vec3 vWorldPosition;
out vec3 vNormal;
out vec4 vTangent;
out vec2 vUv;

void main()
{
    const mat3 normalMatrix = mat3(transpose(inverse(uModel)));
    vNormal = normalMatrix * aNormal;
    vTangent = vec4(mat3(uModel) * aTangent.xyz, aTangent.w);
    vUv = aUv;
    const vec4 world = uModel * vec4(aPosition, 1.0);
    vWorldPosition = world.xyz;
    gl_Position = uViewProjection * world;
}
