#version 450 core
// Game UI (ui::Canvas, M14): framebuffer pixels with +Y down.

layout(location = 0) in vec2 aPosition;
layout(location = 1) in vec2 aUv;
layout(location = 2) in vec4 aColor;        // display values
layout(location = 3) in float aPixelRange;  // > 0: distance-field text; 0: image
layout(location = 4) in float aOutline;     // outline width in pixels (text)
layout(location = 5) in vec4 aOutlineColor; // display values

uniform mat4 uProjection;

out vec2 vUv;
out vec4 vColor;
out float vPixelRange;
out float vOutline;
out vec4 vOutlineColor;

void main()
{
    vUv = aUv;
    vColor = aColor;
    vPixelRange = aPixelRange;
    vOutline = aOutline;
    vOutlineColor = aOutlineColor;
    gl_Position = uProjection * vec4(aPosition, 0.0, 1.0);
}
