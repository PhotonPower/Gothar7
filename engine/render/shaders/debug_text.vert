#version 450 core
// Debug text glyph quads (render::DebugDraw), in pixels with a top-left origin.

layout(location = 0) in vec2 aPixel;
layout(location = 1) in vec2 aUv;
layout(location = 2) in vec4 aColor;
layout(location = 3) in float aDepth; // anchor window depth, < 0 = not depth-tested

uniform vec2 uViewport;

out vec2 vUv;
out vec4 vColor;
out float vDepth;

void main()
{
    vUv = aUv;
    vColor = aColor;
    vDepth = aDepth;
    const vec2 ndc = vec2(aPixel.x / uViewport.x * 2.0 - 1.0, 1.0 - aPixel.y / uViewport.y * 2.0);
    gl_Position = vec4(ndc, 0.0, 1.0);
}
