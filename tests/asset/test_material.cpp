#include <g7/asset/MeshData.hpp>

#include <doctest/doctest.h>

#include <cstring>
#include <ostream> // doctest needs it to print std::string_view operands
#include <string>
#include <vector>

using namespace g7;
using namespace g7::asset;

namespace
{
std::string base64(const std::vector<u8>& data)
{
    static constexpr char kTable[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    for (usize i = 0; i < data.size(); i += 3)
    {
        const u32 triple = (u32(data[i]) << 16) | (i + 1 < data.size() ? u32(data[i + 1]) << 8 : 0u) |
                           (i + 2 < data.size() ? u32(data[i + 2]) : 0u);
        out += kTable[(triple >> 18) & 63];
        out += kTable[(triple >> 12) & 63];
        out += i + 1 < data.size() ? kTable[(triple >> 6) & 63] : '=';
        out += i + 2 < data.size() ? kTable[triple & 63] : '=';
    }
    return out;
}

/// Quad in the XY plane facing +Z with glTF UVs (v = 0 at the top), indexed, one material.
/// `mirrorU` flips the U direction (mirrored texture mapping); `extraAttributes` adds e.g. TANGENT.
std::string quadGltf(const std::string& material, bool mirrorU = false, bool withTangents = false)
{
    const f32 u0 = mirrorU ? 1.0f : 0.0f;
    const f32 u1 = mirrorU ? 0.0f : 1.0f;
    const std::vector<f32> positions = {-1, -1, 0, 1, -1, 0, 1, 1, 0, -1, 1, 0};
    const std::vector<f32> uvs = {u0, 1, u1, 1, u1, 0, u0, 0};
    const std::vector<f32> tangents = {0, 1, 0, -1, 0, 1, 0, -1,
                                       0, 1, 0, -1, 0, 1, 0, -1}; // deliberately odd
    const std::vector<u16> indices = {0, 1, 2, 0, 2, 3};
    std::vector<u8> buffer;
    const auto append = [&](const void* data, usize size)
    {
        const usize offset = buffer.size();
        buffer.resize(offset + size);
        std::memcpy(buffer.data() + offset, data, size);
        return offset;
    };
    append(positions.data(), 48);
    append(uvs.data(), 32);
    append(indices.data(), 12);
    append(tangents.data(), 64);
    const std::string tangentAttribute = withTangents ? R"(,"TANGENT":3)" : "";
    return R"({"asset":{"version":"2.0"},"scene":0,"scenes":[{"nodes":[0]}],"nodes":[{"mesh":0}],)"
           R"("meshes":[{"primitives":[{"attributes":{"POSITION":0,"TEXCOORD_0":1)" +
           tangentAttribute +
           R"(},"indices":2,"material":0}]}],)"
           R"("materials":[)" +
           material +
           R"(],)"
           R"("textures":[{"source":0}],"images":[{"uri":"x.png"}],)"
           R"("accessors":[{"bufferView":0,"componentType":5126,"count":4,"type":"VEC3","min":[-1,-1,0],"max":[1,1,0]},)"
           R"({"bufferView":1,"componentType":5126,"count":4,"type":"VEC2"},)"
           R"({"bufferView":2,"componentType":5123,"count":6,"type":"SCALAR"},)"
           R"({"bufferView":3,"componentType":5126,"count":4,"type":"VEC4"}],)"
           R"("bufferViews":[{"buffer":0,"byteOffset":0,"byteLength":48},{"buffer":0,"byteOffset":48,"byteLength":32},)"
           R"({"buffer":0,"byteOffset":80,"byteLength":12},{"buffer":0,"byteOffset":92,"byteLength":64}],)"
           R"("buffers":[{"byteLength":156,"uri":"data:application/octet-stream;base64,)" +
           base64(buffer) + R"("}]})";
}

MeshData load(const std::string& json)
{
    auto result =
        loadGltf(std::span(reinterpret_cast<const u8*>(json.data()), json.size()), fs::Path("."), "quad");
    REQUIRE_MESSAGE(result.ok(), (result.ok() ? "" : result.error().message));
    return std::move(result).value();
}
} // namespace

TEST_CASE("Material: glTF fields are read")
{
    const MeshData mesh = load(quadGltf(
        R"({"name":"Laub","alphaMode":"MASK","alphaCutoff":0.3,"doubleSided":true,"emissiveFactor":[1,0.5,0],)"
        R"("normalTexture":{"index":0,"scale":0.7},"emissiveTexture":{"index":0},)"
        R"("pbrMetallicRoughness":{"baseColorTexture":{"index":0}}})"));
    REQUIRE(mesh.materials.size() == 1);
    const MaterialInfo& m = mesh.materials[0];
    CHECK(m.name == "Laub");
    CHECK(m.alphaMode == AlphaMode::Mask);
    CHECK(m.alphaCutoff == doctest::Approx(0.3f));
    CHECK(m.doubleSided);
    CHECK(nearlyEqual(m.emissive, Vec3(1.0f, 0.5f, 0.0f)));
    CHECK(m.normalImage == 0);
    CHECK(m.normalScale == doctest::Approx(0.7f));
    CHECK(m.emissiveImage == 0);
    CHECK(m.baseColorImage == 0);

    const MeshData blend = load(quadGltf(R"({"alphaMode":"BLEND"})"));
    CHECK(blend.materials[0].alphaMode == AlphaMode::Blend);
    CHECK_FALSE(blend.materials[0].doubleSided);
    CHECK(blend.materials[0].normalImage == -1);
    CHECK(blend.materials[0].alphaCutoff == doctest::Approx(0.5f)); // glTF default
}

TEST_CASE("Material: tangents are computed for normal-mapped materials")
{
    const MeshData mesh = load(quadGltf(R"({"normalTexture":{"index":0}})"));
    REQUIRE(mesh.vertices.size() == 4);
    for (const Vertex& v : mesh.vertices)
    {
        // +u points to +X; the map's +Y (towards v = 0, the image top) is world +Y, so w = +1.
        CHECK(nearlyEqual(Vec3(v.tangent), Vec3(1, 0, 0), 1e-4f));
        CHECK(v.tangent.w == 1.0f);
        CHECK(nearlyEqual(glm::cross(v.normal, Vec3(v.tangent)) * v.tangent.w, Vec3(0, 1, 0), 1e-4f));
    }
}

TEST_CASE("Material: mirrored UVs flip the tangent handedness")
{
    const MeshData mesh = load(quadGltf(R"({"normalTexture":{"index":0}})", true));
    for (const Vertex& v : mesh.vertices)
    {
        CHECK(nearlyEqual(Vec3(v.tangent), Vec3(-1, 0, 0), 1e-4f));
        CHECK(v.tangent.w == -1.0f);
        // The bitangent still points to the image top.
        CHECK(nearlyEqual(glm::cross(v.normal, Vec3(v.tangent)) * v.tangent.w, Vec3(0, 1, 0), 1e-4f));
    }
}

TEST_CASE("Material: no normal map means no tangents; file tangents are kept")
{
    const MeshData plain = load(quadGltf(R"({"name":"plain"})"));
    for (const Vertex& v : plain.vertices)
    {
        CHECK(v.tangent == Vec4(0.0f));
    }

    const MeshData fromFile = load(quadGltf(R"({"normalTexture":{"index":0}})", false, true));
    CHECK(nearlyEqual(Vec3(fromFile.vertices[0].tangent), Vec3(0, 1, 0), 1e-4f));
    CHECK(fromFile.vertices[0].tangent.w == -1.0f);
}
