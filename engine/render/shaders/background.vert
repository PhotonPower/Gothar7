#version 450 core
// Fullscreen triangle without vertex buffers (gl_VertexID 0..2).

out vec2 vNdc;

void main()
{
    const vec2 position = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2) * 2.0 - 1.0;
    vNdc = position;
    gl_Position = vec4(position, 0.0, 1.0); // depth 0 = far plane (reverse-Z)
}
