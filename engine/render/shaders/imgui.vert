#version 450 core
// Dear ImGui geometry (ui::DebugUi), display coordinates with +Y down.

layout(location = 0) in vec2 aPosition;
layout(location = 1) in vec2 aUv;
layout(location = 2) in vec4 aColor; // display values

uniform mat4 uProjection;

out vec2 vUv;
out vec4 vColor;

void main()
{
    vUv = aUv;
    vColor = aColor;
    gl_Position = uProjection * vec4(aPosition, 0.0, 1.0);
}
