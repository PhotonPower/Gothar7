#include <g7/asset/MeshFile.hpp>
#include <g7/asset/Procedural.hpp>

#include <doctest/doctest.h>

#include <cstring>
#include <ostream> // doctest needs it to print std::string operands
#include <string>
#include <vector>

using namespace g7;
using namespace g7::asset;

namespace
{
MeshData sampleMesh()
{
    MeshData mesh = makeBox(Vec3(1.0f, 2.0f, 0.5f), Vec4(0.8f, 0.6f, 0.4f, 1.0f));
    mesh.vertices[3].tangent = Vec4(1.0f, 0.0f, 0.0f, -1.0f);
    MaterialInfo leaves;
    leaves.name = "Blätter";
    leaves.baseColorImage = 0;
    leaves.normalImage = 1;
    leaves.normalScale = 0.75f;
    leaves.emissive = Vec3(0.1f, 0.2f, 0.3f);
    leaves.alphaMode = AlphaMode::Mask;
    leaves.alphaCutoff = 0.3f;
    leaves.doubleSided = true;
    mesh.materials.push_back(leaves);
    mesh.submeshes.push_back({0, 6, static_cast<u32>(mesh.materials.size() - 1)});
    mesh.images.push_back({"textures/leaves.png", {}, "image/png"});
    mesh.images.push_back({"", {1, 2, 3, 4}, "image/jpeg"});
    return mesh;
}

void checkEqual(const MeshData& a, const MeshData& b)
{
    REQUIRE(a.vertices.size() == b.vertices.size());
    for (usize i = 0; i < a.vertices.size(); ++i)
    {
        CHECK(a.vertices[i].position == b.vertices[i].position);
        CHECK(a.vertices[i].normal == b.vertices[i].normal);
        CHECK(a.vertices[i].uv == b.vertices[i].uv);
        CHECK(a.vertices[i].tangent == b.vertices[i].tangent);
    }
    CHECK(a.indices == b.indices);
    REQUIRE(a.submeshes.size() == b.submeshes.size());
    for (usize i = 0; i < a.submeshes.size(); ++i)
    {
        CHECK(a.submeshes[i].firstIndex == b.submeshes[i].firstIndex);
        CHECK(a.submeshes[i].indexCount == b.submeshes[i].indexCount);
        CHECK(a.submeshes[i].material == b.submeshes[i].material);
    }
    REQUIRE(a.materials.size() == b.materials.size());
    for (usize i = 0; i < a.materials.size(); ++i)
    {
        const MaterialInfo& x = a.materials[i];
        const MaterialInfo& y = b.materials[i];
        CHECK(x.name == y.name);
        CHECK(x.baseColor == y.baseColor);
        CHECK(x.baseColorImage == y.baseColorImage);
        CHECK(x.normalImage == y.normalImage);
        CHECK(x.normalScale == y.normalScale);
        CHECK(x.emissive == y.emissive);
        CHECK(x.emissiveImage == y.emissiveImage);
        CHECK(x.alphaMode == y.alphaMode);
        CHECK(x.alphaCutoff == y.alphaCutoff);
        CHECK(x.doubleSided == y.doubleSided);
    }
    REQUIRE(a.images.size() == b.images.size());
    for (usize i = 0; i < a.images.size(); ++i)
    {
        CHECK(a.images[i].uri == b.images[i].uri);
        CHECK(a.images[i].mimeType == b.images[i].mimeType);
        CHECK(a.images[i].encoded == b.images[i].encoded);
    }
    CHECK(a.bounds.min == b.bounds.min);
    CHECK(a.bounds.max == b.bounds.max);
}

void setU32(std::vector<u8>& data, usize at, u32 v)
{
    for (usize i = 0; i < 4; ++i)
    {
        data[at + i] = static_cast<u8>(v >> (8 * i));
    }
}

constexpr usize kVertexCountAt = 8;
constexpr usize kHeaderSize = 4 + 7 * 4 + 6 * 4;
} // namespace

TEST_CASE(".g7mesh round trip keeps every field")
{
    const MeshData mesh = sampleMesh();
    const std::vector<u8> bytes = serializeMesh(mesh);
    CHECK(std::memcmp(bytes.data(), kMeshMagic, 4) == 0);
    auto loaded = deserializeMesh(bytes, "sample.g7mesh");
    REQUIRE_MESSAGE(loaded, (loaded ? "" : loaded.error().message));
    checkEqual(mesh, loaded.value());
    CHECK(serializeMesh(loaded.value()) == bytes); // deterministic

    const MeshData empty;
    auto loadedEmpty = deserializeMesh(serializeMesh(empty));
    REQUIRE(loadedEmpty);
    CHECK(loadedEmpty.value().vertices.empty());
}

TEST_CASE("corrupt .g7mesh files are rejected")
{
    const std::vector<u8> good = serializeMesh(sampleMesh());
    const auto rejected = [](const std::vector<u8>& data, const char* expected)
    {
        auto result = deserializeMesh(data, "bad.g7mesh");
        REQUIRE_FALSE(result);
        CHECK_MESSAGE(result.error().message.find(expected) != std::string::npos, result.error().message);
    };

    SUBCASE("not a mesh")
    {
        rejected({'n', 'o', 'p', 'e'}, "not a .g7mesh");
        auto data = good;
        data[0] = 'X';
        rejected(data, "not a .g7mesh");
    }
    SUBCASE("unknown version")
    {
        auto data = good;
        setU32(data, 4, 99);
        rejected(data, "version");
    }
    SUBCASE("truncated or trailing data")
    {
        auto data = good;
        data.resize(data.size() - 3);
        rejected(data, "truncated");
        data = good;
        data.push_back(0);
        rejected(data, "trailing");
    }
    SUBCASE("counts larger than the file")
    {
        auto data = good;
        setU32(data, kVertexCountAt, 0x7fffffff);
        rejected(data, "counts exceed");
    }
    SUBCASE("index out of range")
    {
        auto data = good;
        const usize vertexCount = sampleMesh().vertices.size();
        setU32(data, kHeaderSize + vertexCount * 48, 100000); // first index
        rejected(data, "index out of range");
    }
    SUBCASE("submesh and image references")
    {
        MeshData mesh = sampleMesh();
        mesh.submeshes.back().indexCount = 100000;
        rejected(serializeMesh(mesh), "submesh");
        mesh = sampleMesh();
        mesh.submeshes.back().material = 99;
        rejected(serializeMesh(mesh), "submesh");
        mesh = sampleMesh();
        mesh.materials.back().normalImage = 7;
        rejected(serializeMesh(mesh), "image reference");
    }
    SUBCASE("unknown alpha mode")
    {
        MeshData mesh = sampleMesh();
        mesh.materials.back().alphaMode = static_cast<AlphaMode>(9);
        rejected(serializeMesh(mesh), "alpha mode");
    }
}
