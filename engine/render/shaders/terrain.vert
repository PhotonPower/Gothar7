#version 450 core
// Heightmap terrain (render::TerrainRenderer): a chunk grid whose heights come from an R16 texture.
// With SHADOW defined only the position is computed (depth pass into a cascade).

layout(location = 0) in vec3 aGrid; // x, z in samples from the chunk origin; y = 1 on the skirt

layout(binding = 4) uniform sampler2D uHeights; // R16, read with texelFetch

uniform vec2 uSize;        // samples per row, rows
uniform vec2 uFirstSample; // x, z of cell (0, 0)
uniform float uCellSize;
uniform vec2 uHeightRange; // minY, maxY
uniform float uSkirtDepth;
uniform vec2 uChunkOrigin; // first cell of the chunk
uniform mat4 uViewProjection;

#ifndef SHADOW
out vec3 vWorldPosition;
out vec3 vNormal;
#endif

float heightAt(ivec2 cell)
{
    cell = clamp(cell, ivec2(0), ivec2(uSize) - 1);
    return uHeightRange.x + texelFetch(uHeights, cell, 0).r * (uHeightRange.y - uHeightRange.x);
}

void main()
{
    const ivec2 cell = clamp(ivec2(uChunkOrigin + aGrid.xz), ivec2(0), ivec2(uSize) - 1);
    const float height = heightAt(cell) - aGrid.y * uSkirtDepth;
    const vec3 world = vec3(uFirstSample.x + float(cell.x) * uCellSize, height,
                            uFirstSample.y + float(cell.y) * uCellSize);
#ifndef SHADOW
    // Central differences: n ~ (-dh/dx, 1, -dh/dz).
    const float left = heightAt(cell - ivec2(1, 0));
    const float right = heightAt(cell + ivec2(1, 0));
    const float north = heightAt(cell - ivec2(0, 1));
    const float south = heightAt(cell + ivec2(0, 1));
    vNormal = normalize(vec3(left - right, 2.0 * uCellSize, north - south));
    vWorldPosition = world;
#endif
    gl_Position = uViewProjection * vec4(world, 1.0);
}
