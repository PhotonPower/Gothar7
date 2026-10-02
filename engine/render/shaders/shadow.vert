#version 450 core
// Depth-only pass into a shadow cascade (uViewProjection = the cascade's light matrix).

layout(location = 0) in vec3 aPosition;
layout(location = 2) in vec2 aUv;

uniform mat4 uViewProjection;
uniform mat4 uModel;

out vec2 vUv;

void main()
{
    vUv = aUv;
    gl_Position = uViewProjection * uModel * vec4(aPosition, 1.0);
}
