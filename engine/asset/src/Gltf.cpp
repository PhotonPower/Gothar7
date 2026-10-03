#include "GltfCommon.hpp"

#include <g7/asset/MeshData.hpp>
#include <g7/core/Log.hpp>

#include <fastgltf/core.hpp>
#include <fastgltf/glm_element_traits.hpp>
#include <fastgltf/tools.hpp>

#include <cstring>
#include <filesystem>
#include <limits>
#include <map>
#include <string>
#include <variant>

namespace g7::asset
{
namespace gltf
{
/// Indices of one material, collected over all primitives before they are concatenated.
using IndicesByMaterial = std::map<u32, std::vector<u32>>;

Mat4 toGlm(const fastgltf::math::fmat4x4& matrix)
{
    Mat4 result;
    std::memcpy(&result, matrix.data(), sizeof(Mat4)); // both column-major
    return result;
}

i32 imageOf(const fastgltf::Asset& asset, usize textureIndex)
{
    const fastgltf::Texture& texture = asset.textures[textureIndex];
    return texture.imageIndex ? static_cast<i32>(*texture.imageIndex) : -1;
}

std::vector<MaterialInfo> readMaterials(const fastgltf::Asset& asset)
{
    std::vector<MaterialInfo> materials;
    materials.reserve(asset.materials.size());
    for (const fastgltf::Material& material : asset.materials)
    {
        MaterialInfo info;
        info.name = std::string(material.name);
        const auto& factor = material.pbrData.baseColorFactor;
        info.baseColor = Vec4(factor[0], factor[1], factor[2], factor[3]);
        info.baseColorImage = material.pbrData.baseColorTexture
                                  ? imageOf(asset, material.pbrData.baseColorTexture->textureIndex)
                                  : -1;
        if (material.normalTexture)
        {
            info.normalImage = imageOf(asset, material.normalTexture->textureIndex);
            info.normalScale = material.normalTexture->scale;
        }
        info.emissive =
            Vec3(material.emissiveFactor[0], material.emissiveFactor[1], material.emissiveFactor[2]);
        info.emissiveImage =
            material.emissiveTexture ? imageOf(asset, material.emissiveTexture->textureIndex) : -1;
        switch (material.alphaMode)
        {
        case fastgltf::AlphaMode::Mask:
            info.alphaMode = AlphaMode::Mask;
            break;
        case fastgltf::AlphaMode::Blend:
            info.alphaMode = AlphaMode::Blend;
            break;
        default:
            info.alphaMode = AlphaMode::Opaque;
            break;
        }
        info.alphaCutoff = material.alphaCutoff;
        info.doubleSided = material.doubleSided;
        materials.push_back(std::move(info));
    }
    return materials;
}

std::string mimeTypeName(fastgltf::MimeType type)
{
    switch (type)
    {
    case fastgltf::MimeType::PNG:
        return "image/png";
    case fastgltf::MimeType::JPEG:
        return "image/jpeg";
    default:
        return {};
    }
}

std::vector<u8> copyBytes(const std::byte* data, usize size)
{
    std::vector<u8> bytes(size);
    if (size > 0)
    {
        std::memcpy(bytes.data(), data, size);
    }
    return bytes;
}

/// Bytes of a loaded buffer (buffers are loaded into memory, see LoadExternalBuffers).
std::span<const std::byte> bufferBytes(const fastgltf::Buffer& buffer)
{
    if (const auto* array = std::get_if<fastgltf::sources::Array>(&buffer.data))
    {
        return {array->bytes.data(), array->bytes.size()};
    }
    if (const auto* vector = std::get_if<fastgltf::sources::Vector>(&buffer.data))
    {
        return {vector->bytes.data(), vector->bytes.size()};
    }
    return {};
}

std::vector<ImageSource> readImages(const fastgltf::Asset& asset, std::string_view debugName)
{
    std::vector<ImageSource> images;
    images.reserve(asset.images.size());
    for (const fastgltf::Image& image : asset.images)
    {
        ImageSource source;
        if (const auto* uri = std::get_if<fastgltf::sources::URI>(&image.data))
        {
            source.uri = std::string(uri->uri.path());
            source.mimeType = mimeTypeName(uri->mimeType);
        }
        else if (const auto* array = std::get_if<fastgltf::sources::Array>(&image.data))
        {
            source.encoded = copyBytes(array->bytes.data(), array->bytes.size()); // data: URI
            source.mimeType = mimeTypeName(array->mimeType);
        }
        else if (const auto* view = std::get_if<fastgltf::sources::BufferView>(&image.data))
        {
            const fastgltf::BufferView& bufferView = asset.bufferViews[view->bufferViewIndex];
            const auto bytes = bufferBytes(asset.buffers[bufferView.bufferIndex]);
            if (bufferView.byteOffset + bufferView.byteLength <= bytes.size())
            {
                source.encoded = copyBytes(bytes.data() + bufferView.byteOffset, bufferView.byteLength);
            }
            source.mimeType = mimeTypeName(view->mimeType);
        }
        if (source.uri.empty() && source.encoded.empty())
        {
            G7_LOG_WARN("asset", "{}: image '{}' has an unsupported source", debugName, image.name);
        }
        images.push_back(std::move(source));
    }
    return images;
}

/// Per-vertex tangents from UVs (Lengyel), averaged over the triangles and orthogonalised against
/// the normal. Not bit-exact MikkTSpace (that comes with the cooker, M3), but consistent with glTF:
/// +X of the normal map follows +u, +Y points to the image top (decreasing v), and
/// bitangent = cross(normal, tangent) * w.
void computeTangents(std::vector<Vertex>& vertices, std::span<const u32> indices, usize firstVertex)
{
    std::vector<Vec3> uDirections(vertices.size() - firstVertex, Vec3(0.0f));
    std::vector<Vec3> upDirections(vertices.size() - firstVertex, Vec3(0.0f));
    for (usize i = 0; i + 2 < indices.size(); i += 3)
    {
        const u32 ia = indices[i];
        const u32 ib = indices[i + 1];
        const u32 ic = indices[i + 2];
        const Vertex& a = vertices[ia];
        const Vertex& b = vertices[ib];
        const Vertex& c = vertices[ic];
        const Vec3 e1 = b.position - a.position;
        const Vec3 e2 = c.position - a.position;
        const Vec2 d1 = b.uv - a.uv;
        const Vec2 d2 = c.uv - a.uv;
        const f32 det = d1.x * d2.y - d2.x * d1.y;
        if (std::abs(det) < 1e-12f)
        {
            continue; // degenerate UVs contribute nothing
        }
        const f32 r = 1.0f / det;
        const Vec3 dPdu = (e1 * d2.y - e2 * d1.y) * r;
        const Vec3 dPdv = (e2 * d1.x - e1 * d2.x) * r;
        for (const u32 index : {ia, ib, ic})
        {
            uDirections[index - firstVertex] += dPdu;
            upDirections[index - firstVertex] -= dPdv; // glTF: v grows downwards, the map's +Y is up
        }
    }
    for (usize i = firstVertex; i < vertices.size(); ++i)
    {
        const Vec3 n = vertices[i].normal;
        Vec3 t = uDirections[i - firstVertex] - n * glm::dot(n, uDirections[i - firstVertex]);
        if (glm::dot(t, t) < 1e-12f)
        {
            // No usable UVs: any tangent perpendicular to the normal keeps the basis valid.
            t = glm::cross(std::abs(n.y) < 0.99f ? Vec3(0, 1, 0) : Vec3(1, 0, 0), n);
        }
        t = glm::normalize(t);
        const f32 w = glm::dot(glm::cross(n, t), upDirections[i - firstVertex]) < 0.0f ? -1.0f : 1.0f;
        vertices[i].tangent = Vec4(t, w);
    }
}

void computeFlatNormals(std::vector<Vertex>& vertices, std::span<const u32> indices, usize firstVertex)
{
    for (usize i = firstVertex; i < vertices.size(); ++i)
    {
        vertices[i].normal = Vec3(0.0f);
    }
    for (usize i = 0; i + 2 < indices.size(); i += 3)
    {
        Vertex& a = vertices[indices[i]];
        Vertex& b = vertices[indices[i + 1]];
        Vertex& c = vertices[indices[i + 2]];
        // Area-weighted: summing unnormalised cross products favours larger triangles.
        const Vec3 n = glm::cross(b.position - a.position, c.position - a.position);
        a.normal += n;
        b.normal += n;
        c.normal += n;
    }
    for (usize i = firstVertex; i < vertices.size(); ++i)
    {
        const f32 length = glm::length(vertices[i].normal);
        vertices[i].normal = length > 0.0f ? vertices[i].normal / length : Vec3(0, 1, 0);
    }
}

Result<fastgltf::Asset> parseAsset(fastgltf::GltfDataBuffer& data, const fs::Path& baseDirectory,
                                   std::string_view debugName)
{
    // Without a base directory only self-contained data is allowed (GLB chunk, data: URIs).
    // fastgltf still wants an existing directory, but reads nothing from it then.
    const bool external = !baseDirectory.empty();
    fs::Path directory = baseDirectory;
    if (!external)
    {
        std::error_code ec;
        directory = std::filesystem::current_path(ec);
        if (ec)
        {
            directory = std::filesystem::temp_directory_path(ec);
        }
    }
    fastgltf::Parser parser;
    auto asset = parser.loadGltf(data, directory,
                                 external ? fastgltf::Options::LoadExternalBuffers : fastgltf::Options::None);
    if (asset.error() != fastgltf::Error::None)
    {
        return Error{std::string(debugName) + ": " + std::string(fastgltf::getErrorMessage(asset.error()))};
    }
    if (!external)
    {
        for (const fastgltf::Buffer& buffer : asset->buffers)
        {
            if (const auto* uri = std::get_if<fastgltf::sources::URI>(&buffer.data))
            {
                return Error{std::string(debugName) + ": external buffer '" + std::string(uri->uri.path()) +
                             "' needs a directory on disk (use a self-contained .glb)"};
            }
        }
    }
    return std::move(asset.get());
}
} // namespace gltf

namespace
{
using namespace gltf;

Result<void> appendPrimitive(const fastgltf::Asset& asset, const fastgltf::Primitive& primitive,
                             const Mat4& world, u32 material, MeshData& mesh,
                             IndicesByMaterial& indicesByMaterial, bool needsTangents)
{
    const auto positionIt = primitive.findAttribute("POSITION");
    if (positionIt == primitive.attributes.end())
    {
        return Error{"primitive without POSITION"};
    }
    const fastgltf::Accessor& positions = asset.accessors[positionIt->accessorIndex];
    const usize firstVertex = mesh.vertices.size();
    if (firstVertex + positions.count > std::numeric_limits<u32>::max())
    {
        return Error{"mesh too large for 32-bit indices"};
    }
    mesh.vertices.resize(firstVertex + positions.count);

    const Mat3 linear(world);
    const Mat3 normalMatrix = glm::transpose(glm::inverse(linear));
    const bool mirrored = glm::determinant(linear) < 0.0f;

    fastgltf::iterateAccessorWithIndex<Vec3>(
        asset, positions,
        [&](Vec3 p, usize i) { mesh.vertices[firstVertex + i].position = Vec3(world * Vec4(p, 1.0f)); });
    const auto normalIt = primitive.findAttribute("NORMAL");
    if (normalIt != primitive.attributes.end())
    {
        fastgltf::iterateAccessorWithIndex<Vec3>(
            asset, asset.accessors[normalIt->accessorIndex], [&](Vec3 n, usize i)
            { mesh.vertices[firstVertex + i].normal = glm::normalize(normalMatrix * n); });
    }
    const auto uvIt = primitive.findAttribute("TEXCOORD_0");
    if (uvIt != primitive.attributes.end())
    {
        fastgltf::iterateAccessorWithIndex<Vec2>(asset, asset.accessors[uvIt->accessorIndex],
                                                 [&](Vec2 uv, usize i)
                                                 { mesh.vertices[firstVertex + i].uv = uv; });
    }
    const auto tangentIt = primitive.findAttribute("TANGENT");
    if (tangentIt != primitive.attributes.end())
    {
        fastgltf::iterateAccessorWithIndex<Vec4>(asset, asset.accessors[tangentIt->accessorIndex],
                                                 [&](Vec4 t, usize i)
                                                 {
                                                     mesh.vertices[firstVertex + i].tangent =
                                                         Vec4(glm::normalize(linear * Vec3(t)),
                                                              mirrored ? -t.w : t.w);
                                                 });
    }

    std::vector<u32> indices;
    if (primitive.indicesAccessor)
    {
        const fastgltf::Accessor& accessor = asset.accessors[*primitive.indicesAccessor];
        indices.resize(accessor.count);
        fastgltf::copyFromAccessor<u32>(asset, accessor, indices.data());
    }
    else
    {
        indices.resize(positions.count);
        for (usize i = 0; i < indices.size(); ++i)
        {
            indices[i] = static_cast<u32>(i);
        }
    }
    for (u32& index : indices)
    {
        if (index >= positions.count)
        {
            return Error{"index out of range"};
        }
        index += static_cast<u32>(firstVertex);
    }
    if (mirrored)
    {
        // Negative scale flips the winding; swap to keep front faces counter-clockwise.
        for (usize i = 0; i + 2 < indices.size(); i += 3)
        {
            std::swap(indices[i + 1], indices[i + 2]);
        }
    }
    if (normalIt == primitive.attributes.end())
    {
        computeFlatNormals(mesh.vertices, indices, firstVertex);
    }
    if (needsTangents && tangentIt == primitive.attributes.end())
    {
        computeTangents(mesh.vertices, indices, firstVertex);
    }

    auto& target = indicesByMaterial[material];
    target.insert(target.end(), indices.begin(), indices.end());
    return {};
}

/// A triangle primitive of a COL_ node as a collision part (positions in model space).
Result<void> appendCollision(const fastgltf::Asset& asset, const fastgltf::Primitive& primitive,
                             const Mat4& world, std::string_view nodeName, MeshData& mesh)
{
    const auto positionIt = primitive.findAttribute("POSITION");
    if (positionIt == primitive.attributes.end())
    {
        return Error{"collision primitive without POSITION"};
    }
    const fastgltf::Accessor& positions = asset.accessors[positionIt->accessorIndex];
    std::vector<Vec3> local(positions.count);
    fastgltf::copyFromAccessor<Vec3>(asset, positions, local.data());
    if (local.empty())
    {
        return {};
    }
    CollisionPart part;
    if (nodeName.starts_with(kCollisionBoxPrefix))
    {
        // Bounds in node space, so a turned node gives a turned box.
        Vec3 lo = local.front();
        Vec3 hi = local.front();
        for (const Vec3& p : local)
        {
            lo = glm::min(lo, p);
            hi = glm::max(hi, p);
        }
        part.kind = CollisionPart::Kind::Hull;
        for (int i = 0; i < 8; ++i)
        {
            const Vec3 corner(i & 1 ? hi.x : lo.x, i & 2 ? hi.y : lo.y, i & 4 ? hi.z : lo.z);
            part.points.push_back(Vec3(world * Vec4(corner, 1.0f)));
        }
    }
    else
    {
        part.kind = nodeName.starts_with(kCollisionHullPrefix) ? CollisionPart::Kind::Hull
                                                               : CollisionPart::Kind::Mesh;
        part.points.reserve(local.size());
        for (const Vec3& p : local)
        {
            part.points.push_back(Vec3(world * Vec4(p, 1.0f)));
        }
        if (part.kind == CollisionPart::Kind::Mesh)
        {
            if (primitive.indicesAccessor)
            {
                const fastgltf::Accessor& accessor = asset.accessors[*primitive.indicesAccessor];
                part.indices.resize(accessor.count);
                fastgltf::copyFromAccessor<u32>(asset, accessor, part.indices.data());
            }
            else
            {
                part.indices.resize(local.size());
                for (usize i = 0; i < part.indices.size(); ++i)
                {
                    part.indices[i] = static_cast<u32>(i);
                }
            }
            for (const u32 index : part.indices)
            {
                if (index >= part.points.size())
                {
                    return Error{"collision index out of range"};
                }
            }
            if (glm::determinant(Mat3(world)) < 0.0f)
            {
                for (usize i = 0; i + 2 < part.indices.size(); i += 3)
                {
                    std::swap(part.indices[i + 1], part.indices[i + 2]);
                }
            }
        }
    }
    mesh.collision.push_back(std::move(part));
    return {};
}

Result<MeshData> convert(fastgltf::Asset& asset, std::string_view debugName)
{
    MeshData mesh;
    mesh.materials = readMaterials(asset);
    mesh.images = readImages(asset, debugName);
    const u32 defaultMaterial = static_cast<u32>(mesh.materials.size()); // added only if used
    IndicesByMaterial indicesByMaterial;
    Result<void> failure;
    usize skipped = 0;

    if (asset.scenes.empty())
    {
        return Error{std::string(debugName) + ": glTF has no scene"};
    }
    const usize scene = asset.defaultScene.value_or(0);
    fastgltf::iterateSceneNodes(
        asset, scene, fastgltf::math::fmat4x4(),
        [&](fastgltf::Node& node, const auto& matrix)
        {
            if (!node.meshIndex || !failure)
            {
                return;
            }
            const Mat4 world = toGlm(matrix);
            const std::string_view nodeName(node.name.data(), node.name.size());
            const bool collision = nodeName.starts_with(kCollisionPrefix);
            for (const fastgltf::Primitive& primitive : asset.meshes[*node.meshIndex].primitives)
            {
                if (primitive.type != fastgltf::PrimitiveType::Triangles)
                {
                    ++skipped;
                    continue;
                }
                if (collision)
                {
                    if (auto result = appendCollision(asset, primitive, world, nodeName, mesh); !result)
                    {
                        failure = Error{std::string(debugName) + ": " + std::string(nodeName) + ": " +
                                        result.error().message};
                        return;
                    }
                    continue;
                }
                const u32 material =
                    primitive.materialIndex ? static_cast<u32>(*primitive.materialIndex) : defaultMaterial;
                const bool needsTangents =
                    material < mesh.materials.size() && mesh.materials[material].normalImage >= 0;
                if (auto result = appendPrimitive(asset, primitive, world, material, mesh, indicesByMaterial,
                                                  needsTangents);
                    !result)
                {
                    failure = Error{std::string(debugName) + ": " + result.error().message};
                    return;
                }
            }
        });
    if (!failure)
    {
        return failure.error();
    }
    if (skipped > 0)
    {
        G7_LOG_WARN("asset", "{}: skipped {} non-triangle primitive(s)", debugName, skipped);
    }
    if (indicesByMaterial.contains(defaultMaterial))
    {
        mesh.materials.push_back(MaterialInfo{.name = "default"});
    }

    for (auto& [material, indices] : indicesByMaterial)
    {
        mesh.submeshes.push_back(
            {static_cast<u32>(mesh.indices.size()), static_cast<u32>(indices.size()), material});
        mesh.indices.insert(mesh.indices.end(), indices.begin(), indices.end());
    }
    if (!mesh.vertices.empty())
    {
        mesh.bounds = AABB{mesh.vertices.front().position, mesh.vertices.front().position};
        for (const Vertex& vertex : mesh.vertices)
        {
            mesh.bounds.min = glm::min(mesh.bounds.min, vertex.position);
            mesh.bounds.max = glm::max(mesh.bounds.max, vertex.position);
        }
    }
    return mesh;
}

Result<MeshData> parse(fastgltf::GltfDataBuffer& data, const fs::Path& baseDirectory,
                       std::string_view debugName)
{
    auto asset = parseAsset(data, baseDirectory, debugName);
    if (!asset)
    {
        return asset.error();
    }
    return convert(asset.value(), debugName);
}
} // namespace

Result<MeshData> loadGltf(const fs::Path& path)
{
    if (!fs::exists(path))
    {
        // fastgltf reports a missing file as an invalid directory; say what is wrong instead.
        std::error_code ec;
        const fs::Path absolute = std::filesystem::absolute(path, ec);
        return Error{"glTF file not found: '" + fs::toUtf8(ec ? path : absolute) + "'"};
    }
    auto data = fastgltf::GltfDataBuffer::FromPath(path);
    if (data.error() != fastgltf::Error::None)
    {
        return Error{"cannot read glTF '" + fs::toUtf8(path) +
                     "': " + std::string(fastgltf::getErrorMessage(data.error()))};
    }
    return parse(data.get(), path.parent_path(), fs::toUtf8(path));
}

Result<MeshData> loadGltf(std::span<const u8> bytes, const fs::Path& baseDirectory,
                          std::string_view debugName)
{
    auto data =
        fastgltf::GltfDataBuffer::FromBytes(reinterpret_cast<const std::byte*>(bytes.data()), bytes.size());
    if (data.error() != fastgltf::Error::None)
    {
        return Error{std::string(debugName) + ": " + std::string(fastgltf::getErrorMessage(data.error()))};
    }
    return parse(data.get(), baseDirectory, debugName);
}
} // namespace g7::asset
