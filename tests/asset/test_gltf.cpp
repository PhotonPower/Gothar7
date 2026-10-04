#include <g7/asset/MeshData.hpp>

#include <doctest/doctest.h>

#include <chrono>
#include <cstring>
#include <filesystem>
#include <format>
#include <optional>
#include <ostream> // doctest needs it to print std::string_view operands
#include <string>
#include <vector>

using namespace g7;
using namespace g7::asset;

namespace
{
/// Builds a glTF JSON document with one embedded (data: URI) buffer.
class GltfBuilder
{
public:
    u32 addFloats(const std::vector<f32>& values, u32 components, std::string_view type)
    {
        const u32 offset = append(values.data(), values.size() * sizeof(f32));
        std::string minMax;
        if (components == 3 && type == "VEC3")
        {
            Vec3 mn(1e30f), mx(-1e30f);
            for (usize i = 0; i + 2 < values.size(); i += 3)
            {
                mn = glm::min(mn, Vec3(values[i], values[i + 1], values[i + 2]));
                mx = glm::max(mx, Vec3(values[i], values[i + 1], values[i + 2]));
            }
            minMax = std::format(R"(,"min":[{},{},{}],"max":[{},{},{}])", mn.x, mn.y, mn.z, mx.x, mx.y, mx.z);
        }
        return addAccessor(offset, values.size() * sizeof(f32), 5126, values.size() / components, type,
                           minMax);
    }

    u32 addIndices16(const std::vector<u16>& values)
    {
        const u32 offset = append(values.data(), values.size() * sizeof(u16));
        return addAccessor(offset, values.size() * sizeof(u16), 5123, values.size(), "SCALAR", "");
    }

    u32 addIndices8(const std::vector<u8>& values)
    {
        const u32 offset = append(values.data(), values.size());
        return addAccessor(offset, values.size(), 5121, values.size(), "SCALAR", "");
    }

    std::string primitive(u32 position, std::optional<u32> normal, std::optional<u32> indices,
                          std::optional<u32> material, int mode = 4) const
    {
        std::string attributes = std::format(R"("POSITION":{})", position);
        if (normal)
        {
            attributes += std::format(R"(,"NORMAL":{})", *normal);
        }
        std::string result = std::format(R"({{"attributes":{{{}}},"mode":{})", attributes, mode);
        if (indices)
        {
            result += std::format(R"(,"indices":{})", *indices);
        }
        if (material)
        {
            result += std::format(R"(,"material":{})", *material);
        }
        return result + "}";
    }

    /// `meshes`: JSON of each mesh's primitive arrays; `nodes`: JSON node objects; `materials`: JSON.
    std::string json(const std::vector<std::string>& meshes, const std::vector<std::string>& nodes,
                     const std::string& materials = "[]") const
    {
        std::string meshJson, nodeJson, rootNodes;
        for (usize i = 0; i < meshes.size(); ++i)
        {
            meshJson += (i ? "," : "") + std::string(R"({"primitives":[)") + meshes[i] + "]}";
        }
        for (usize i = 0; i < nodes.size(); ++i)
        {
            nodeJson += (i ? "," : "") + nodes[i];
            rootNodes += (i ? "," : "") + std::to_string(i);
        }
        return std::format(
            R"({{"asset":{{"version":"2.0"}},"scene":0,"scenes":[{{"nodes":[{}]}}],"nodes":[{}],"meshes":[{}],)"
            R"("materials":{},"accessors":[{}],"bufferViews":[{}],)"
            R"("buffers":[{{"byteLength":{},"uri":"data:application/octet-stream;base64,{}"}}]}})",
            rootNodes, nodeJson, meshJson, materials, m_accessors, m_views, m_buffer.size(),
            base64(m_buffer));
    }

private:
    u32 append(const void* data, usize size)
    {
        while (m_buffer.size() % 4 != 0)
        {
            m_buffer.push_back(0);
        }
        const auto offset = static_cast<u32>(m_buffer.size());
        m_buffer.resize(m_buffer.size() + size);
        std::memcpy(m_buffer.data() + offset, data, size);
        return offset;
    }

    u32 addAccessor(u32 offset, usize length, int componentType, usize count, std::string_view type,
                    const std::string& extra)
    {
        const u32 index = m_count++;
        m_views += std::format(R"({}{{"buffer":0,"byteOffset":{},"byteLength":{}}})", index ? "," : "",
                               offset, length);
        m_accessors += std::format(R"({}{{"bufferView":{},"componentType":{},"count":{},"type":"{}"{}}})",
                                   index ? "," : "", index, componentType, count, type, extra);
        return index;
    }

    static std::string base64(const std::vector<u8>& data)
    {
        static constexpr char kTable[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        std::string out;
        for (usize i = 0; i < data.size(); i += 3)
        {
            const u32 b0 = data[i];
            const u32 b1 = i + 1 < data.size() ? data[i + 1] : 0;
            const u32 b2 = i + 2 < data.size() ? data[i + 2] : 0;
            const u32 triple = (b0 << 16) | (b1 << 8) | b2;
            out += kTable[(triple >> 18) & 63];
            out += kTable[(triple >> 12) & 63];
            out += i + 1 < data.size() ? kTable[(triple >> 6) & 63] : '=';
            out += i + 2 < data.size() ? kTable[triple & 63] : '=';
        }
        return out;
    }

    std::vector<u8> m_buffer;
    std::string m_accessors;
    std::string m_views;
    u32 m_count = 0;
};

Result<MeshData> load(const std::string& json)
{
    return loadGltf(std::span(reinterpret_cast<const u8*>(json.data()), json.size()), fs::Path("."),
                    "test.gltf");
}

MeshData require(Result<MeshData> result)
{
    REQUIRE_MESSAGE(result.ok(), (result.ok() ? "" : result.error().message));
    return std::move(result).value();
}

const std::vector<f32> kTriangle = {0, 0, 0, 1, 0, 0, 0, 1, 0};
} // namespace

TEST_CASE("glTF: triangle with normals and 16-bit indices")
{
    GltfBuilder b;
    const u32 pos = b.addFloats(kTriangle, 3, "VEC3");
    const u32 nrm = b.addFloats({0, 0, 1, 0, 0, 1, 0, 0, 1}, 3, "VEC3");
    const u32 idx = b.addIndices16({0, 1, 2});
    const MeshData mesh =
        require(load(b.json({b.primitive(pos, nrm, idx, std::nullopt)}, {R"({"mesh":0})"})));

    REQUIRE(mesh.vertices.size() == 3);
    CHECK(mesh.indices == std::vector<u32>{0, 1, 2});
    CHECK(mesh.vertices[1].position == Vec3(1, 0, 0));
    CHECK(mesh.vertices[2].normal == Vec3(0, 0, 1));
    REQUIRE(mesh.submeshes.size() == 1);
    CHECK(mesh.submeshes[0].indexCount == 3);
    REQUIRE(mesh.materials.size() == 1); // default material added for the unassigned primitive
    CHECK(mesh.materials[0].name == "default");
    CHECK(mesh.bounds.min == Vec3(0.0f));
    CHECK(mesh.bounds.max == Vec3(1, 1, 0));
}

TEST_CASE("glTF: 8-bit indices and generated indices")
{
    GltfBuilder b;
    const u32 pos = b.addFloats(kTriangle, 3, "VEC3");
    const u32 idx = b.addIndices8({2, 1, 0});
    const MeshData mesh = require(load(b.json({b.primitive(pos, std::nullopt, idx, std::nullopt),
                                               b.primitive(pos, std::nullopt, std::nullopt, std::nullopt)},
                                              {R"({"mesh":0})", R"({"mesh":1})"})));
    CHECK(mesh.vertices.size() == 6);
    CHECK(mesh.indices == std::vector<u32>{2, 1, 0, 3, 4, 5}); // second primitive: implicit 0..2, offset
}

TEST_CASE("glTF: missing normals are computed")
{
    GltfBuilder b;
    const u32 pos = b.addFloats(kTriangle, 3, "VEC3");
    const MeshData mesh = require(
        load(b.json({b.primitive(pos, std::nullopt, std::nullopt, std::nullopt)}, {R"({"mesh":0})"})));
    for (const Vertex& v : mesh.vertices)
    {
        CHECK(nearlyEqual(v.normal, Vec3(0, 0, 1))); // counter-clockwise in the XY plane faces +Z
    }
}

TEST_CASE("glTF: node transforms are baked in")
{
    GltfBuilder b;
    const u32 pos = b.addFloats(kTriangle, 3, "VEC3");
    const u32 nrm = b.addFloats({0, 0, 1, 0, 0, 1, 0, 0, 1}, 3, "VEC3");
    // Parent moves +10 x, child rotates 90° around Y (quaternion x,y,z,w) and scales by 2.
    const std::string nodes0 = R"({"translation":[10,0,0],"children":[1]})";
    const std::string nodes1 = R"({"mesh":0,"rotation":[0,0.7071068,0,0.7071068],"scale":[2,2,2]})";
    std::string json = b.json({b.primitive(pos, nrm, std::nullopt, std::nullopt)}, {nodes0, nodes1});
    // The builder lists every node as a scene root; only node 0 should be a root here.
    json.replace(json.find(R"("nodes":[0,1])"), 13, R"("nodes":[0])");
    const MeshData mesh = require(load(json));

    REQUIRE(mesh.vertices.size() == 3);
    CHECK(nearlyEqual(mesh.vertices[0].position, Vec3(10, 0, 0), 1e-4f));
    CHECK(nearlyEqual(mesh.vertices[1].position, Vec3(10, 0, -2), 1e-4f)); // x axis -> -z, scaled
    CHECK(nearlyEqual(mesh.vertices[2].position, Vec3(10, 2, 0), 1e-4f));
    CHECK(nearlyEqual(mesh.vertices[0].normal, Vec3(1, 0, 0), 1e-4f)); // +z -> +x, unit length
}

TEST_CASE("glTF: mirrored nodes keep counter-clockwise front faces")
{
    GltfBuilder b;
    const u32 pos = b.addFloats(kTriangle, 3, "VEC3");
    const u32 idx = b.addIndices16({0, 1, 2});
    const MeshData mesh = require(load(
        b.json({b.primitive(pos, std::nullopt, idx, std::nullopt)}, {R"({"mesh":0,"scale":[-1,1,1]})"})));
    CHECK(mesh.indices == std::vector<u32>{0, 2, 1});
    CHECK(nearlyEqual(mesh.vertices[0].normal, Vec3(0, 0, 1))); // still faces +Z after the flip
}

TEST_CASE("glTF: COL_ nodes become collision parts and are not drawn")
{
    GltfBuilder b;
    const u32 pos = b.addFloats(kTriangle, 3, "VEC3");
    const u32 idx = b.addIndices16({0, 1, 2});
    // A cube-ish point cloud: corners (0..1)^3.
    const u32 cube =
        b.addFloats({0, 0, 0, 1, 0, 0, 0, 1, 0, 1, 1, 0, 0, 0, 1, 1, 0, 1, 0, 1, 1, 1, 1, 1}, 3, "VEC3");
    const std::string render = b.primitive(pos, std::nullopt, idx, std::nullopt);
    const std::string cubePrim = b.primitive(cube, std::nullopt, std::nullopt, std::nullopt);
    const MeshData mesh = require(load(b.json(
        {render, cubePrim},
        {R"({"mesh":0,"name":"Wall"})", R"({"mesh":0,"name":"COL_Floor","translation":[0,5,0]})",
         R"({"mesh":1,"name":"COL_HULL_Body"})",
         R"({"mesh":1,"name":"COL_BOX_Turned","rotation":[0,0.7071068,0,0.7071068],"scale":[2,1,1]})"})));

    CHECK(mesh.vertices.size() == 3); // only "Wall" is drawn
    CHECK(mesh.bounds.max.y == doctest::Approx(1.0f));
    REQUIRE(mesh.collision.size() == 3);
    const CollisionPart& floor = mesh.collision[0];
    CHECK(floor.kind == CollisionPart::Kind::Mesh);
    CHECK(floor.indices == std::vector<u32>{0, 1, 2});
    CHECK(nearlyEqual(floor.points[2], Vec3(0, 6, 0), 1e-5f));
    CHECK(mesh.collision[1].kind == CollisionPart::Kind::Hull);
    CHECK(mesh.collision[1].points.size() == 8);
    CHECK(mesh.collision[1].indices.empty());
    // The box: node-space bounds (0..1)^3 scaled 2 in x, then turned 90° about Y -> x 0..1, z -2..0.
    const CollisionPart& box = mesh.collision[2];
    CHECK(box.kind == CollisionPart::Kind::Hull);
    REQUIRE(box.points.size() == 8);
    Vec3 lo(1e9f), hi(-1e9f);
    for (const Vec3& p : box.points)
    {
        lo = glm::min(lo, p);
        hi = glm::max(hi, p);
    }
    CHECK(nearlyEqual(lo, Vec3(0, 0, -2), 1e-4f));
    CHECK(nearlyEqual(hi, Vec3(1, 1, 0), 1e-4f));
}

TEST_CASE("glTF: submeshes per material, non-triangles skipped")
{
    GltfBuilder b;
    const u32 pos = b.addFloats(kTriangle, 3, "VEC3");
    const std::string materials =
        R"([{"name":"Holz","pbrMetallicRoughness":{"baseColorFactor":[0.5,0.25,0.1,1]}},{"name":"Stein"}])";
    const std::string mesh0 = b.primitive(pos, std::nullopt, std::nullopt, 1) + "," +
                              b.primitive(pos, std::nullopt, std::nullopt, 0) + "," +
                              b.primitive(pos, std::nullopt, std::nullopt, 1) + "," +
                              b.primitive(pos, std::nullopt, std::nullopt, 0, 1); // lines
    const MeshData mesh = require(load(b.json({mesh0}, {R"({"mesh":0})"}, materials)));

    REQUIRE(mesh.materials.size() == 2);
    CHECK(mesh.materials[0].name == "Holz");
    CHECK(nearlyEqual(Vec3(mesh.materials[0].baseColor), Vec3(0.5f, 0.25f, 0.1f)));
    REQUIRE(mesh.submeshes.size() == 2);
    CHECK(mesh.submeshes[0].material == 0);
    CHECK(mesh.submeshes[0].indexCount == 3);
    CHECK(mesh.submeshes[1].material == 1);
    CHECK(mesh.submeshes[1].firstIndex == 3);
    CHECK(mesh.submeshes[1].indexCount == 6); // both "Stein" primitives merged
    CHECK(mesh.vertices.size() == 9);         // the line primitive contributed nothing
}

TEST_CASE("glTF: errors")
{
    CHECK_FALSE(load("{ not json").ok());
    CHECK_FALSE(load(R"({"asset":{"version":"2.0"}})").ok()); // no scene
    auto missing = loadGltf(fs::Path("does/not/exist.gltf"));
    REQUIRE_FALSE(missing.ok());
    CHECK(missing.error().message.find("exist.gltf") != std::string::npos);
    CHECK(missing.error().message.find("not found") != std::string::npos);
}

TEST_CASE("glTF: external .bin and .glb files")
{
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    const fs::Path dir = std::filesystem::temp_directory_path() / ("g7_gltf_test_" + std::to_string(stamp));
    REQUIRE(fs::createDirectories(dir).ok());

    std::vector<u8> bin(kTriangle.size() * sizeof(f32));
    std::memcpy(bin.data(), kTriangle.data(), bin.size());
    REQUIRE(fs::writeFile(dir / "tri.bin", bin).ok());
    const std::string gltf =
        R"({"asset":{"version":"2.0"},"scene":0,"scenes":[{"nodes":[0]}],"nodes":[{"mesh":0}],)"
        R"("meshes":[{"primitives":[{"attributes":{"POSITION":0}}]}],)"
        R"("accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3","min":[0,0,0],"max":[1,1,0]}],)"
        R"("bufferViews":[{"buffer":0,"byteOffset":0,"byteLength":36}],"buffers":[{"byteLength":36,"uri":"tri.bin"}]})";
    REQUIRE(fs::writeText(dir / "tri.gltf", gltf).ok());
    const MeshData external = require(loadGltf(dir / "tri.gltf"));
    CHECK(external.vertices.size() == 3);

    // GLB: header, JSON chunk (padded with spaces), BIN chunk.
    std::string json = gltf;
    json.replace(json.find(R"(,"uri":"tri.bin")"), 16, "");
    while (json.size() % 4 != 0)
    {
        json += ' ';
    }
    std::vector<u8> glb;
    const auto put32 = [&](u32 v)
    {
        for (int i = 0; i < 4; ++i)
        {
            glb.push_back(static_cast<u8>(v >> (8 * i)));
        }
    };
    const u32 total = 12 + 8 + static_cast<u32>(json.size()) + 8 + static_cast<u32>(bin.size());
    put32(0x46546C67); // "glTF"
    put32(2);
    put32(total);
    put32(static_cast<u32>(json.size()));
    put32(0x4E4F534A); // "JSON"
    glb.insert(glb.end(), json.begin(), json.end());
    put32(static_cast<u32>(bin.size()));
    put32(0x004E4942); // "BIN\0"
    glb.insert(glb.end(), bin.begin(), bin.end());
    REQUIRE(fs::writeFile(dir / "tri.glb", glb).ok());
    const MeshData binary = require(loadGltf(dir / "tri.glb"));
    CHECK(binary.vertices.size() == 3);
    CHECK(binary.vertices[2].position == Vec3(0, 1, 0));

    std::error_code ignored;
    std::filesystem::remove_all(dir, ignored);
}

TEST_CASE("glTF: LOD nodes (_lod1, _lod2) become submeshes of their level")
{
    CHECK(lodLevel("it_bread_lod0") == 0);
    CHECK(lodLevel("it_bread_lod1") == 1);
    CHECK(lodLevel("WALL_lod2") == 2);
    CHECK(lodLevel("WALL") == 0);
    CHECK(lodLevel("flood") == 0);
    CHECK(lodLevel("x_lod") == 0);
    CHECK(lodLevel("x_lodA") == 0);
    CHECK(isCoarserLod("WALL_lod1"));
    CHECK_FALSE(isCoarserLod("WALL_lod0"));
    // figuren's items carry _lod0 .. _lod2: three levels, the full one within the budget (<= 628 triangles).
    const MeshData sword = require(loadGltf(fs::fromUtf8(G7_ASSET_SOURCE_DIR "/items/it_sword_old.glb")));
    CHECK(maxLod(sword) == 2);
    u32 triangles[3] = {};
    for (const Submesh& s : sword.submeshes)
    {
        REQUIRE(s.lod <= 2);
        triangles[s.lod] += s.indexCount / 3;
    }
    CHECK(triangles[0] <= 628);
    CHECK(triangles[1] < triangles[0]);
    CHECK(triangles[2] < triangles[1]);
    // Levels lie on top of each other (same origin): the bounds are those of one blade.
    const Vec3 size = sword.bounds.max - sword.bounds.min;
    CHECK(size.y == doctest::Approx(1.035f).epsilon(0.01));
    // Submeshes are ordered by level: level 0 first.
    CHECK(sword.submeshes.front().lod == 0);
}
