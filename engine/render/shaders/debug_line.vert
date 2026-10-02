#version 450 core
// Debug lines (render::DebugDraw), world space.

layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec4 aColor; // display values

uniform mat4 uViewProjection;

out vec4 vColor;

void main()
{
    vColor = aColor;
    gl_Position = uViewProjection * vec4(aPosition, 1.0);
}
