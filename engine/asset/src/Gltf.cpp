#include <g7/asset/MeshData.hpp>
#include <g7/core/Log.hpp>

#include <fastgltf/core.hpp>
#include <fastgltf/glm_element_traits.hpp>
#include <fastgltf/tools.hpp>

#include <cstring>
#include <limits>
#include <map>
#include <string>
#include <variant>

namespace g7::asset
{
namespace
{
/// Indices of one material, collected over all primitives before they are concatenated.
using IndicesByMaterial = std::map<u32, std::vector<u32>>;

Mat4 toGlm(const fastgltf::math::fmat4x4& matrix)
{
    Mat4 result;
    std::memcpy(&result, matrix.data(), sizeof(Mat4)); // both column-major
    return result;
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
        if (material.pbrData.baseColorTexture)
        {
            const fastgltf::Texture& texture =
                asset.textures[material.pbrData.baseColorTexture->textureIndex];
            if (texture.imageIndex)
            {
                const auto& image = asset.images[*texture.imageIndex];
                if (const auto* uri = std::get_if<fastgltf::sources::URI>(&image.data))
                {
                    info.baseColorTexture = std::string(uri->uri.path());
                }
            }
        }
        materials.push_back(std::move(info));
    }
    return materials;
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

Result<void> appendPrimitive(const fastgltf::Asset& asset, const fastgltf::Primitive& primitive,
                             const Mat4& world, u32 material, MeshData& mesh,
                             IndicesByMaterial& indicesByMaterial)
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

    auto& target = indicesByMaterial[material];
    target.insert(target.end(), indices.begin(), indices.end());
    return {};
}

Result<MeshData> convert(fastgltf::Asset& asset, std::string_view debugName)
{
    MeshData mesh;
    mesh.materials = readMaterials(asset);
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
            for (const fastgltf::Primitive& primitive : asset.meshes[*node.meshIndex].primitives)
            {
                if (primitive.type != fastgltf::PrimitiveType::Triangles)
                {
                    ++skipped;
                    continue;
                }
                const u32 material =
                    primitive.materialIndex ? static_cast<u32>(*primitive.materialIndex) : defaultMaterial;
                if (auto result = appendPrimitive(asset, primitive, world, material, mesh, indicesByMaterial);
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
        mesh.materials.push_back(MaterialInfo{"default", Vec4(1.0f), {}});
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
    fastgltf::Parser parser;
    auto asset = parser.loadGltf(data, baseDirectory, fastgltf::Options::LoadExternalBuffers);
    if (asset.error() != fastgltf::Error::None)
    {
        return Error{std::string(debugName) + ": " + std::string(fastgltf::getErrorMessage(asset.error()))};
    }
    return convert(asset.get(), debugName);
}
} // namespace

Result<MeshData> loadGltf(const fs::Path& path)
{
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
