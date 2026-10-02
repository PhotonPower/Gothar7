#version 450 core
// Static mesh (asset::Vertex layout). Placeholder lighting until the material/light tasks.

layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUv;
layout(location = 3) in vec4 aTangent;

uniform mat4 uViewProjection;
uniform mat4 uModel;

out vec3 vNormal;
out vec2 vUv;

void main()
{
    vNormal = mat3(transpose(inverse(uModel))) * aNormal;
    vUv = aUv;
    gl_Position = uViewProjection * uModel * vec4(aPosition, 1.0);
}
