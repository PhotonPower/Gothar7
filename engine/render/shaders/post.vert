#version 450 core
// Fullscreen triangle without vertex buffers (gl_VertexID 0..2).

out vec2 vUv;

void main()
{
    const vec2 position = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);
    vUv = position;
    gl_Position = vec4(position * 2.0 - 1.0, 0.0, 1.0);
}
