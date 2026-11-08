#include "ByteIo.hpp"

#include <g7/asset/SkinFile.hpp>

#include <array>
#include <bit>
#include <cstring>
#include <limits>
#include <string>

namespace g7::asset
{
namespace
{
using detail::ByteReader;
using detail::ByteWriter;

// Smallest records, to refuse counts that cannot fit before allocating.
constexpr u64 kMinBoneSize = 2 + 4 + 10 * 4;
constexpr u64 kMatrixSize = 16 * 4;
constexpr u64 kMinMaterialSize = 2 + 4 * 4 + 4 + 4 + 4 + 3 * 4 + 4 + 1 + 4 + 1 + 1 + 4 + 3 * 4;
constexpr u64 kMinImageSize = 2 + 2 + 4;
constexpr u64 kMinPartSize = 2 + 2 + 7 * 4;
constexpr u64 kVertexRecordSize = 12 * 4 + 4 * 2 + 4 * 4; // vertex, joints, weights
constexpr u64 kSubmeshSize = 4 * 4;
constexpr u64 kPrimitiveSize = 5 * 4;
constexpr u64 kMinMorphSize = 2 + 1;

std::string_view clamp16(const std::string& s)
{
    return std::string_view(s).substr(0, std::numeric_limits<u16>::max());
}

Error skinError(std::string_view debugName, const std::string& what)
{
    return Error{std::string(debugName) + ": corrupt .g7skin (" + what + ")"};
}

void writeFloats(ByteWriter& w, const f32* v, int n)
{
    for (int i = 0; i < n; ++i)
    {
        w.f32v(v[i]);
    }
}

void readFloats(ByteReader& r, f32* v, int n)
{
    for (int i = 0; i < n; ++i)
    {
        v[i] = r.f32v();
    }
}

void writeQuat(ByteWriter& w, const Quat& q)
{
    w.f32v(q.x);
    w.f32v(q.y);
    w.f32v(q.z);
    w.f32v(q.w);
}

Quat readQuat(ByteReader& r)
{
    Quat q;
    q.x = r.f32v();
    q.y = r.f32v();
    q.z = r.f32v();
    q.w = r.f32v();
    return q;
}

void writeMatrix(ByteWriter& w, const Mat4& m)
{
    for (int c = 0; c < 4; ++c)
    {
        writeFloats(w, &m[c].x, 4);
    }
}

Mat4 readMatrix(ByteReader& r)
{
    Mat4 m(1.0f);
    for (int c = 0; c < 4; ++c)
    {
        readFloats(r, &m[c].x, 4);
    }
    return m;
}

/// A count read from the file that its records of at least `minSize` bytes must fit into.
bool fits(const ByteReader& r, u64 count, u64 minSize)
{
    return r.has(count * minSize);
}
} // namespace

std::vector<u8> serializeSkin(const SkinnedModelData& model)
{
    std::vector<u8> out;
    ByteWriter w(out);
    w.text(std::string_view(kSkinMagic, sizeof(kSkinMagic)));
    w.u32v(kSkinVersion);

    const SkeletonData& s = model.skeleton;
    w.u32v(static_cast<u32>(s.size()));
    for (usize b = 0; b < s.size(); ++b)
    {
        w.string16(clamp16(s.names[b]));
        w.i32v(s.parents[b]);
        writeFloats(w, &s.translations[b].x, 3);
        writeQuat(w, s.rotations[b]);
        writeFloats(w, &s.scales[b].x, 3);
    }
    writeMatrix(w, s.rootParent);
    for (usize b = 0; b < s.size(); ++b)
    {
        writeMatrix(w, b < model.inverseBind.size() ? model.inverseBind[b] : Mat4(1.0f));
    }
    writeFloats(w, &model.bounds.min.x, 3);
    writeFloats(w, &model.bounds.max.x, 3);

    w.u32v(static_cast<u32>(model.materials.size()));
    for (const MaterialInfo& m : model.materials)
    {
        w.string16(clamp16(m.name));
        writeFloats(w, &m.baseColor.x, 4);
        w.i32v(m.baseColorImage);
        w.i32v(m.normalImage);
        w.f32v(m.normalScale);
        writeFloats(w, &m.emissive.x, 3);
        w.i32v(m.emissiveImage);
        w.u8v(static_cast<u8>(m.alphaMode));
        w.f32v(m.alphaCutoff);
        w.u8v(m.doubleSided ? 1 : 0);
        w.u8v(static_cast<u8>(m.wind));
        w.f32v(m.windStrength);
        writeFloats(w, &m.windPivot.x, 3);
    }
    w.u32v(static_cast<u32>(model.images.size()));
    for (const ImageSource& img : model.images)
    {
        w.string16(clamp16(img.uri));
        w.string16(clamp16(img.mimeType));
        w.blob32(img.encoded);
    }

    w.u32v(static_cast<u32>(model.parts.size()));
    for (const SkinnedPartData& p : model.parts)
    {
        w.string16(clamp16(p.node));
        w.string16(clamp16(p.role));
        w.u32v(p.lod);
        w.u32v(static_cast<u32>(p.vertices.size()));
        w.u32v(static_cast<u32>(p.indices.size()));
        w.u32v(static_cast<u32>(p.submeshes.size()));
        w.u32v(static_cast<u32>(p.morphs.size()));
        w.u32v(static_cast<u32>(p.primitives.size()));
        for (const Vertex& v : p.vertices)
        {
            writeFloats(w, &v.position.x, 3);
            writeFloats(w, &v.normal.x, 3);
            writeFloats(w, &v.uv.x, 2);
            writeFloats(w, &v.tangent.x, 4);
        }
        for (usize i = 0; i < p.vertices.size(); ++i)
        {
            const std::array<u16, 4> j = i < p.joints.size() ? p.joints[i] : std::array<u16, 4>{};
            for (const u16 bone : j)
            {
                w.u16v(bone);
            }
        }
        for (usize i = 0; i < p.vertices.size(); ++i)
        {
            const Vec4 weight = i < p.weights.size() ? p.weights[i] : Vec4(1.0f, 0.0f, 0.0f, 0.0f);
            writeFloats(w, &weight.x, 4);
        }
        for (const u32 index : p.indices)
        {
            w.u32v(index);
        }
        for (const Submesh& sm : p.submeshes)
        {
            w.u32v(sm.firstIndex);
            w.u32v(sm.indexCount);
            w.u32v(sm.material);
            w.u32v(sm.lod);
        }
        for (const MorphTargetData& morph : p.morphs)
        {
            w.string16(clamp16(morph.name));
            const bool normals = !morph.normals.empty();
            w.u8v(normals ? 1 : 0);
            for (usize i = 0; i < p.vertices.size(); ++i)
            {
                const Vec3 d = i < morph.positions.size() ? morph.positions[i] : Vec3(0.0f);
                writeFloats(w, &d.x, 3);
            }
            for (usize i = 0; normals && i < p.vertices.size(); ++i)
            {
                const Vec3 d = i < morph.normals.size() ? morph.normals[i] : Vec3(0.0f);
                writeFloats(w, &d.x, 3);
            }
        }
        for (const SkinnedPartData::Primitive& prim : p.primitives)
        {
            w.u32v(prim.firstVertex);
            w.u32v(prim.vertexCount);
            w.u32v(prim.firstIndex);
            w.u32v(prim.indexCount);
            w.u32v(prim.material);
        }
    }

    const SkinnedModelData::Assembly& a = model.assembly;
    w.u32v(a.version);
    w.string16(clamp16(a.part));
    w.u32v(static_cast<u32>(a.neck.size()));
    for (const auto& [node, ring] : a.neck)
    {
        w.string16(std::string_view(node).substr(0, std::numeric_limits<u16>::max()));
        w.u32v(static_cast<u32>(ring.size()));
        for (const auto& point : ring)
        {
            w.u32v(static_cast<u32>(point.size()));
            for (const auto& [primitive, vertex] : point)
            {
                w.u32v(primitive);
                w.u32v(vertex);
            }
        }
    }
    w.u32v(static_cast<u32>(a.falloff.size()));
    for (const auto& [node, list] : a.falloff)
    {
        w.string16(std::string_view(node).substr(0, std::numeric_limits<u16>::max()));
        w.u32v(static_cast<u32>(list.size()));
        for (const SkinnedModelData::Assembly::Falloff& f : list)
        {
            w.u32v(f.primitive);
            w.u32v(f.vertex);
            w.u32v(f.ringPoint);
            w.u64v(std::bit_cast<u64>(f.weight));
        }
    }
    w.string16(clamp16(a.coversBody));
    w.u32v(static_cast<u32>(a.covers.size()));
    for (const auto& [node, ranges] : a.covers)
    {
        w.string16(std::string_view(node).substr(0, std::numeric_limits<u16>::max()));
        w.u32v(static_cast<u32>(ranges.size()));
        for (const auto& range : ranges)
        {
            for (const u32 v : range)
            {
                w.u32v(v);
            }
        }
    }
    w.u32v(static_cast<u32>(a.hides.size()));
    for (const std::string& role : a.hides)
    {
        w.string16(clamp16(role));
    }
    return out;
}

Result<SkinnedModelData> deserializeSkin(std::span<const u8> bytes, std::string_view debugName)
{
    if (bytes.size() < 8 || std::memcmp(bytes.data(), kSkinMagic, sizeof(kSkinMagic)) != 0)
    {
        return Error{std::string(debugName) + ": not a .g7skin file"};
    }
    ByteReader r(bytes);
    static_cast<void>(r.bytes(4));
    const u32 version = r.u32v();
    if (version != kSkinVersion)
    {
        return Error{std::string(debugName) + ": .g7skin version " + std::to_string(version) + ", expected " +
                     std::to_string(kSkinVersion) + " (cook again)"};
    }
    SkinnedModelData model;

    const u32 boneCount = r.u32v();
    if (boneCount > kMaxBones || !fits(r, boneCount, kMinBoneSize + kMatrixSize))
    {
        return skinError(debugName, "bone count");
    }
    SkeletonData& s = model.skeleton;
    s.names.resize(boneCount);
    s.parents.resize(boneCount);
    s.translations.resize(boneCount);
    s.rotations.resize(boneCount);
    s.scales.resize(boneCount);
    for (u32 b = 0; b < boneCount; ++b)
    {
        s.names[b] = r.string16();
        s.parents[b] = r.i32v();
        readFloats(r, &s.translations[b].x, 3);
        s.rotations[b] = readQuat(r);
        readFloats(r, &s.scales[b].x, 3);
        if (s.parents[b] < -1 || s.parents[b] >= static_cast<i32>(b))
        {
            return skinError(debugName, "bone parent after its child");
        }
    }
    s.rootParent = readMatrix(r);
    model.inverseBind.resize(boneCount);
    for (Mat4& m : model.inverseBind)
    {
        m = readMatrix(r);
    }
    readFloats(r, &model.bounds.min.x, 3);
    readFloats(r, &model.bounds.max.x, 3);

    const u32 materialCount = r.u32v();
    if (!fits(r, materialCount, kMinMaterialSize))
    {
        return skinError(debugName, "material count");
    }
    model.materials.resize(materialCount);
    for (MaterialInfo& m : model.materials)
    {
        m.name = r.string16();
        readFloats(r, &m.baseColor.x, 4);
        m.baseColorImage = r.i32v();
        m.normalImage = r.i32v();
        m.normalScale = r.f32v();
        readFloats(r, &m.emissive.x, 3);
        m.emissiveImage = r.i32v();
        const u8 alpha = r.u8v();
        m.alphaCutoff = r.f32v();
        m.doubleSided = r.u8v() != 0;
        const u8 wind = r.u8v();
        m.windStrength = r.f32v();
        readFloats(r, &m.windPivot.x, 3);
        if (alpha > static_cast<u8>(AlphaMode::Blend) || wind > static_cast<u8>(WindKind::Hanging))
        {
            return skinError(debugName, "unknown alpha mode or wind");
        }
        m.alphaMode = static_cast<AlphaMode>(alpha);
        m.wind = static_cast<WindKind>(wind);
    }
    const u32 imageCount = r.u32v();
    if (!fits(r, imageCount, kMinImageSize))
    {
        return skinError(debugName, "image count");
    }
    model.images.resize(imageCount);
    for (ImageSource& img : model.images)
    {
        img.uri = r.string16();
        img.mimeType = r.string16();
        img.encoded = r.blob32();
    }

    const u32 partCount = r.u32v();
    if (!fits(r, partCount, kMinPartSize))
    {
        return skinError(debugName, "part count");
    }
    model.parts.resize(partCount);
    for (SkinnedPartData& p : model.parts)
    {
        p.node = r.string16();
        p.role = r.string16();
        p.lod = r.u32v();
        const u32 vertexCount = r.u32v();
        const u32 indexCount = r.u32v();
        const u32 submeshCount = r.u32v();
        const u32 morphCount = r.u32v();
        const u32 primitiveCount = r.u32v();
        if (!fits(r, vertexCount, kVertexRecordSize) || !fits(r, indexCount, 4) ||
            !fits(r, submeshCount, kSubmeshSize) || !fits(r, primitiveCount, kPrimitiveSize) ||
            !fits(r, morphCount, kMinMorphSize + u64(vertexCount) * 12))
        {
            return skinError(debugName, "part counts");
        }
        p.vertices.resize(vertexCount);
        for (Vertex& v : p.vertices)
        {
            readFloats(r, &v.position.x, 3);
            readFloats(r, &v.normal.x, 3);
            readFloats(r, &v.uv.x, 2);
            readFloats(r, &v.tangent.x, 4);
        }
        p.joints.resize(vertexCount);
        for (auto& j : p.joints)
        {
            for (u16& bone : j)
            {
                bone = r.u16v();
                if (bone >= boneCount)
                {
                    return skinError(debugName, "joint out of range");
                }
            }
        }
        p.weights.resize(vertexCount);
        for (Vec4& weight : p.weights)
        {
            readFloats(r, &weight.x, 4);
        }
        p.indices.resize(indexCount);
        for (u32& index : p.indices)
        {
            index = r.u32v();
            if (index >= vertexCount)
            {
                return skinError(debugName, "index out of range");
            }
        }
        p.submeshes.resize(submeshCount);
        for (Submesh& sm : p.submeshes)
        {
            sm.firstIndex = r.u32v();
            sm.indexCount = r.u32v();
            sm.material = r.u32v();
            sm.lod = r.u32v();
            if (sm.firstIndex > indexCount || sm.indexCount > indexCount - sm.firstIndex ||
                sm.material >= materialCount)
            {
                return skinError(debugName, "submesh out of range");
            }
        }
        p.morphs.resize(morphCount);
        for (MorphTargetData& morph : p.morphs)
        {
            morph.name = r.string16();
            const bool normals = r.u8v() != 0;
            if (!fits(r, vertexCount, normals ? 24 : 12))
            {
                return skinError(debugName, "morph target");
            }
            morph.positions.resize(vertexCount);
            for (Vec3& d : morph.positions)
            {
                readFloats(r, &d.x, 3);
            }
            if (normals)
            {
                morph.normals.resize(vertexCount);
                for (Vec3& d : morph.normals)
                {
                    readFloats(r, &d.x, 3);
                }
            }
        }
        p.primitives.resize(primitiveCount);
        for (SkinnedPartData::Primitive& prim : p.primitives)
        {
            prim.firstVertex = r.u32v();
            prim.vertexCount = r.u32v();
            prim.firstIndex = r.u32v();
            prim.indexCount = r.u32v();
            prim.material = r.u32v();
            if (prim.firstVertex > vertexCount || prim.vertexCount > vertexCount - prim.firstVertex ||
                prim.firstIndex > indexCount || prim.indexCount > indexCount - prim.firstIndex)
            {
                return skinError(debugName, "primitive out of range");
            }
        }
        if (r.failed())
        {
            return skinError(debugName, "truncated");
        }
    }

    SkinnedModelData::Assembly& a = model.assembly;
    a.version = r.u32v();
    a.part = r.string16();
    const u32 neckCount = r.u32v();
    for (u32 n = 0; n < neckCount && !r.failed(); ++n)
    {
        std::string node = r.string16();
        const u32 ringCount = r.u32v();
        if (!fits(r, ringCount, 4))
        {
            return skinError(debugName, "neck ring");
        }
        std::vector<std::vector<std::array<u32, 2>>> ring(ringCount);
        for (auto& point : ring)
        {
            const u32 count = r.u32v();
            if (!fits(r, count, 8))
            {
                return skinError(debugName, "neck ring point");
            }
            point.resize(count);
            for (auto& [primitive, vertex] : point)
            {
                primitive = r.u32v();
                vertex = r.u32v();
            }
        }
        a.neck.emplace(std::move(node), std::move(ring));
    }
    const u32 falloffCount = r.u32v();
    for (u32 n = 0; n < falloffCount && !r.failed(); ++n)
    {
        std::string node = r.string16();
        const u32 count = r.u32v();
        if (!fits(r, count, 20))
        {
            return skinError(debugName, "falloff");
        }
        std::vector<SkinnedModelData::Assembly::Falloff> list(count);
        for (auto& f : list)
        {
            f.primitive = r.u32v();
            f.vertex = r.u32v();
            f.ringPoint = r.u32v();
            f.weight = std::bit_cast<f64>(r.u64v());
        }
        a.falloff.emplace(std::move(node), std::move(list));
    }
    a.coversBody = r.string16();
    const u32 coversCount = r.u32v();
    for (u32 n = 0; n < coversCount && !r.failed(); ++n)
    {
        std::string node = r.string16();
        const u32 count = r.u32v();
        if (!fits(r, count, 12))
        {
            return skinError(debugName, "covers");
        }
        std::vector<std::array<u32, 3>> ranges(count);
        for (auto& range : ranges)
        {
            for (u32& v : range)
            {
                v = r.u32v();
            }
        }
        a.covers.emplace(std::move(node), std::move(ranges));
    }
    const u32 hideCount = r.u32v();
    if (!fits(r, hideCount, 2))
    {
        return skinError(debugName, "hides");
    }
    a.hides.resize(hideCount);
    for (std::string& role : a.hides)
    {
        role = r.string16();
    }

    if (r.failed())
    {
        return skinError(debugName, "truncated");
    }
    if (r.remaining() != 0)
    {
        return skinError(debugName, "trailing data");
    }
    const auto validImage = [imageCount](i32 index) { return index >= -1 && index < i32(imageCount); };
    for (const MaterialInfo& m : model.materials)
    {
        if (!validImage(m.baseColorImage) || !validImage(m.normalImage) || !validImage(m.emissiveImage))
        {
            return skinError(debugName, "image reference out of range");
        }
    }
    return model;
}
} // namespace g7::asset
