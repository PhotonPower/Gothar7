#include "ByteIo.hpp"

#include <g7/asset/MeshFile.hpp>

#include <cstring>
#include <limits>
#include <string>

namespace g7::asset
{
namespace
{
using detail::ByteReader;
using detail::ByteWriter;

constexpr usize kHeaderSize = 4 + 7 * 4 + 6 * 4;
constexpr usize kVertexSize = 12 * 4;
constexpr usize kSubmeshSize = 3 * 4;
// Smallest material / image records (empty strings and blobs).
constexpr usize kMinMaterialSize = 2 + 4 * 4 + 4 + 4 + 4 + 3 * 4 + 4 + 1 + 4 + 1;
constexpr usize kMinImageSize = 2 + 2 + 4;

void writeVec(ByteWriter& w, const f32* v, int n)
{
    for (int i = 0; i < n; ++i)
    {
        w.f32v(v[i]);
    }
}

void readVec(ByteReader& r, f32* v, int n)
{
    for (int i = 0; i < n; ++i)
    {
        v[i] = r.f32v();
    }
}

std::string_view clamp16(const std::string& s)
{
    return std::string_view(s).substr(0, std::numeric_limits<u16>::max());
}

Error meshError(std::string_view debugName, const std::string& what)
{
    return Error{std::string(debugName) + ": " + what};
}
} // namespace

std::vector<u8> serializeMesh(const MeshData& mesh)
{
    std::vector<u8> out;
    out.reserve(kHeaderSize + mesh.vertices.size() * kVertexSize + mesh.indices.size() * 4);
    ByteWriter w(out);
    w.text(std::string_view(kMeshMagic, sizeof(kMeshMagic)));
    w.u32v(kMeshVersion);
    w.u32v(static_cast<u32>(mesh.vertices.size()));
    w.u32v(static_cast<u32>(mesh.indices.size()));
    w.u32v(static_cast<u32>(mesh.submeshes.size()));
    w.u32v(static_cast<u32>(mesh.materials.size()));
    w.u32v(static_cast<u32>(mesh.images.size()));
    w.u32v(static_cast<u32>(mesh.collision.size()));
    writeVec(w, &mesh.bounds.min.x, 3);
    writeVec(w, &mesh.bounds.max.x, 3);

    for (const Vertex& v : mesh.vertices)
    {
        writeVec(w, &v.position.x, 3);
        writeVec(w, &v.normal.x, 3);
        writeVec(w, &v.uv.x, 2);
        writeVec(w, &v.tangent.x, 4);
    }
    for (const u32 i : mesh.indices)
    {
        w.u32v(i);
    }
    for (const Submesh& s : mesh.submeshes)
    {
        w.u32v(s.firstIndex);
        w.u32v(s.indexCount);
        w.u32v(s.material);
        w.u32v(s.lod); // version 3
    }
    for (const MaterialInfo& m : mesh.materials)
    {
        w.string16(clamp16(m.name));
        writeVec(w, &m.baseColor.x, 4);
        w.i32v(m.baseColorImage);
        w.i32v(m.normalImage);
        w.f32v(m.normalScale);
        writeVec(w, &m.emissive.x, 3);
        w.i32v(m.emissiveImage);
        w.u8v(static_cast<u8>(m.alphaMode));
        w.f32v(m.alphaCutoff);
        w.u8v(m.doubleSided ? 1 : 0);
    }
    for (const ImageSource& img : mesh.images)
    {
        w.string16(clamp16(img.uri));
        w.string16(clamp16(img.mimeType));
        w.blob32(img.encoded);
    }
    for (const CollisionPart& part : mesh.collision)
    {
        w.u8v(static_cast<u8>(part.kind));
        w.u32v(static_cast<u32>(part.points.size()));
        w.u32v(static_cast<u32>(part.indices.size()));
        for (const Vec3& p : part.points)
        {
            writeVec(w, &p.x, 3);
        }
        for (const u32 i : part.indices)
        {
            w.u32v(i);
        }
    }
    return out;
}

Result<MeshData> deserializeMesh(std::span<const u8> bytes, std::string_view debugName)
{
    ByteReader r(bytes);
    if (bytes.size() < kHeaderSize || std::memcmp(bytes.data(), kMeshMagic, sizeof(kMeshMagic)) != 0)
    {
        return meshError(debugName, "not a .g7mesh file");
    }
    (void)r.text(sizeof(kMeshMagic));
    const u32 version = r.u32v();
    if (version < kMeshMinVersion || version > kMeshVersion)
    {
        return meshError(debugName, "unsupported .g7mesh version " + std::to_string(version));
    }
    const u32 vertexCount = r.u32v();
    const u32 indexCount = r.u32v();
    const u32 submeshCount = r.u32v();
    const u32 materialCount = r.u32v();
    const u32 imageCount = r.u32v();
    const u32 collisionCount = r.u32v(); // version 1: reserved, always 0

    // Reject counts that cannot fit before allocating anything.
    const u64 fixed = u64(vertexCount) * kVertexSize + u64(indexCount) * 4 +
                      u64(submeshCount) * kSubmeshSize + u64(materialCount) * kMinMaterialSize +
                      u64(imageCount) * kMinImageSize;
    if (fixed > r.remaining())
    {
        return meshError(debugName, "corrupt .g7mesh (counts exceed the file size)");
    }

    MeshData mesh;
    readVec(r, &mesh.bounds.min.x, 3);
    readVec(r, &mesh.bounds.max.x, 3);
    mesh.vertices.resize(vertexCount);
    for (Vertex& v : mesh.vertices)
    {
        readVec(r, &v.position.x, 3);
        readVec(r, &v.normal.x, 3);
        readVec(r, &v.uv.x, 2);
        readVec(r, &v.tangent.x, 4);
    }
    mesh.indices.resize(indexCount);
    for (u32& i : mesh.indices)
    {
        i = r.u32v();
    }
    mesh.submeshes.resize(submeshCount);
    for (Submesh& s : mesh.submeshes)
    {
        s.firstIndex = r.u32v();
        s.indexCount = r.u32v();
        s.material = r.u32v();
        s.lod = version >= 3 ? r.u32v() : 0;
    }
    mesh.materials.resize(materialCount);
    for (MaterialInfo& m : mesh.materials)
    {
        m.name = r.string16();
        readVec(r, &m.baseColor.x, 4);
        m.baseColorImage = r.i32v();
        m.normalImage = r.i32v();
        m.normalScale = r.f32v();
        readVec(r, &m.emissive.x, 3);
        m.emissiveImage = r.i32v();
        const u8 alpha = r.u8v();
        m.alphaMode = static_cast<AlphaMode>(alpha);
        m.alphaCutoff = r.f32v();
        m.doubleSided = r.u8v() != 0;
        if (alpha > static_cast<u8>(AlphaMode::Blend))
        {
            return meshError(debugName, "corrupt .g7mesh (unknown alpha mode)");
        }
    }
    mesh.images.resize(imageCount);
    for (ImageSource& img : mesh.images)
    {
        img.uri = r.string16();
        img.mimeType = r.string16();
        img.encoded = r.blob32();
    }
    if (version == 1 && collisionCount != 0)
    {
        return meshError(debugName, "corrupt .g7mesh (reserved field set)");
    }
    for (u32 c = 0; c < collisionCount && !r.failed(); ++c)
    {
        CollisionPart part;
        const u8 kind = r.u8v();
        const u32 pointCount = r.u32v();
        const u32 partIndexCount = r.u32v();
        if (kind > static_cast<u8>(CollisionPart::Kind::Mesh))
        {
            return meshError(debugName, "corrupt .g7mesh (unknown collision kind)");
        }
        if (u64(pointCount) * 12 + u64(partIndexCount) * 4 > r.remaining())
        {
            return meshError(debugName, "corrupt .g7mesh (truncated collision part)");
        }
        part.kind = static_cast<CollisionPart::Kind>(kind);
        part.points.resize(pointCount);
        for (Vec3& p : part.points)
        {
            readVec(r, &p.x, 3);
        }
        part.indices.resize(partIndexCount);
        for (u32& i : part.indices)
        {
            i = r.u32v();
            if (i >= pointCount)
            {
                return meshError(debugName, "corrupt .g7mesh (collision index out of range)");
            }
        }
        mesh.collision.push_back(std::move(part));
    }
    if (r.failed())
    {
        return meshError(debugName, "corrupt .g7mesh (truncated)");
    }
    if (r.remaining() != 0)
    {
        return meshError(debugName, "corrupt .g7mesh (trailing data)");
    }

    for (const u32 i : mesh.indices)
    {
        if (i >= vertexCount)
        {
            return meshError(debugName, "corrupt .g7mesh (index out of range)");
        }
    }
    for (const Submesh& s : mesh.submeshes)
    {
        if (s.firstIndex > indexCount || s.indexCount > indexCount - s.firstIndex ||
            s.material >= materialCount)
        {
            return meshError(debugName, "corrupt .g7mesh (submesh out of range)");
        }
    }
    const auto validImage = [imageCount](i32 index) { return index >= -1 && index < i32(imageCount); };
    for (const MaterialInfo& m : mesh.materials)
    {
        if (!validImage(m.baseColorImage) || !validImage(m.normalImage) || !validImage(m.emissiveImage))
        {
            return meshError(debugName, "corrupt .g7mesh (image reference out of range)");
        }
    }
    return mesh;
}
} // namespace g7::asset
