#include <g7/asset/Procedural.hpp>

namespace g7::asset
{
namespace
{
/// Appends one quad (counter-clockwise seen from the front). Corners: bottom-left, bottom-right,
/// top-right, top-left as seen from the front; glTF UVs (v = 0 at the top).
void addQuad(MeshData& mesh, const Vec3& bl, const Vec3& br, const Vec3& tr, const Vec3& tl,
             const Vec3& normal, const Vec2& uvScale)
{
    const auto base = static_cast<u32>(mesh.vertices.size());
    // Tangent along +u (bl -> br); handedness +1 because the bitangent cross(n, t) points to +v-up.
    const Vec4 tangent(glm::normalize(br - bl), 1.0f);
    mesh.vertices.push_back({bl, normal, Vec2(0.0f, uvScale.y), tangent});
    mesh.vertices.push_back({br, normal, Vec2(uvScale.x, uvScale.y), tangent});
    mesh.vertices.push_back({tr, normal, Vec2(uvScale.x, 0.0f), tangent});
    mesh.vertices.push_back({tl, normal, Vec2(0.0f, 0.0f), tangent});
    for (const u32 i : {0u, 1u, 2u, 0u, 2u, 3u})
    {
        mesh.indices.push_back(base + i);
    }
}

void finish(MeshData& mesh, const Vec4& color, const char* name)
{
    mesh.submeshes = {{0, static_cast<u32>(mesh.indices.size()), 0}};
    MaterialInfo material;
    material.name = name;
    material.baseColor = color;
    mesh.materials = {material};
    mesh.bounds = AABB{mesh.vertices.front().position, mesh.vertices.front().position};
    for (const Vertex& v : mesh.vertices)
    {
        mesh.bounds.min = glm::min(mesh.bounds.min, v.position);
        mesh.bounds.max = glm::max(mesh.bounds.max, v.position);
    }
}
} // namespace

MeshData makePlane(f32 size, f32 uvTileSize, const Vec4& color)
{
    MeshData mesh;
    const f32 h = size * 0.5f;
    const f32 tiles = uvTileSize > 0.0f ? size / uvTileSize : 1.0f;
    // Seen from above (+Y), "up" in the image is -Z.
    addQuad(mesh, Vec3(-h, 0, h), Vec3(h, 0, h), Vec3(h, 0, -h), Vec3(-h, 0, -h), Vec3(0, 1, 0), Vec2(tiles));
    finish(mesh, color, "plane");
    return mesh;
}

MeshData makeBox(const Vec3& e, const Vec4& color)
{
    MeshData mesh;
    const Vec2 one(1.0f);
    // +Z, -Z, +X, -X, +Y, -Y faces, each counter-clockwise seen from outside.
    addQuad(mesh, Vec3(-e.x, -e.y, e.z), Vec3(e.x, -e.y, e.z), Vec3(e.x, e.y, e.z), Vec3(-e.x, e.y, e.z),
            Vec3(0, 0, 1), one);
    addQuad(mesh, Vec3(e.x, -e.y, -e.z), Vec3(-e.x, -e.y, -e.z), Vec3(-e.x, e.y, -e.z), Vec3(e.x, e.y, -e.z),
            Vec3(0, 0, -1), one);
    addQuad(mesh, Vec3(e.x, -e.y, e.z), Vec3(e.x, -e.y, -e.z), Vec3(e.x, e.y, -e.z), Vec3(e.x, e.y, e.z),
            Vec3(1, 0, 0), one);
    addQuad(mesh, Vec3(-e.x, -e.y, -e.z), Vec3(-e.x, -e.y, e.z), Vec3(-e.x, e.y, e.z), Vec3(-e.x, e.y, -e.z),
            Vec3(-1, 0, 0), one);
    addQuad(mesh, Vec3(-e.x, e.y, e.z), Vec3(e.x, e.y, e.z), Vec3(e.x, e.y, -e.z), Vec3(-e.x, e.y, -e.z),
            Vec3(0, 1, 0), one);
    addQuad(mesh, Vec3(-e.x, -e.y, -e.z), Vec3(e.x, -e.y, -e.z), Vec3(e.x, -e.y, e.z), Vec3(-e.x, -e.y, e.z),
            Vec3(0, -1, 0), one);
    finish(mesh, color, "box");
    return mesh;
}
} // namespace g7::asset
