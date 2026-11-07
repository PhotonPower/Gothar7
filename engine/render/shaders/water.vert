#version 450 core
// Water surface (M17 W1, render::WaterRenderer): a rectangle from gl_VertexID (two triangles), no vertex buffer.

uniform mat4 uViewProjection;
uniform vec3 uCentre;      // the surface's centre (y = water level)
uniform vec2 uYaw;         // cos, sin of the turn about +Y
uniform vec2 uHalfExtents; // local x, z

out vec3 vWorld;

void main()
{
    const vec2 corners[6] = vec2[](vec2(-1.0, -1.0), vec2(1.0, -1.0), vec2(1.0, 1.0), vec2(-1.0, -1.0),
                                   vec2(1.0, 1.0), vec2(-1.0, 1.0));
    const vec2 local = corners[gl_VertexID] * uHalfExtents;
    vWorld = uCentre + vec3(uYaw.x * local.x + uYaw.y * local.y, 0.0, -uYaw.y * local.x + uYaw.x * local.y);
    gl_Position = uViewProjection * vec4(vWorld, 1.0);
}
