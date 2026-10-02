#include "../asset/TempDir.hpp"
#include "Cooker.hpp"

#include <g7/asset/AssetManager.hpp>
#include <g7/asset/ImageData.hpp>
#include <g7/asset/MeshData.hpp>
#include <g7/asset/MeshFile.hpp>
#include <g7/asset/Vfs.hpp>

#include <doctest/doctest.h>

#if defined(_MSC_VER)
#pragma warning(push, 0)
#endif
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STBI_WRITE_NO_STDIO
#include <stb_image_write.h>
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <ostream> // doctest needs it to print std::string operands
#include <string>
#include <string_view>
#include <vector>

using namespace g7;

namespace
{
void appendBytes(void* context, void* data, int size)
{
    auto* out = static_cast<std::vector<u8>*>(context);
    const auto* bytes = static_cast<const u8*>(data);
    out->insert(out->end(), bytes, bytes + size);
}

/// 2 x 2 RGBA PNG.
std::vector<u8> png()
{
    const u8 pixels[16] = {255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255, 255, 255, 255, 255};
    std::vector<u8> out;
    REQUIRE(stbi_write_png_to_func(appendBytes, &out, 2, 2, 4, pixels, 8) != 0);
    return out;
}

void put32(std::vector<u8>& out, u32 v)
{
    for (int i = 0; i < 4; ++i)
    {
        out.push_back(static_cast<u8>(v >> (8 * i)));
    }
}

std::vector<u8> triangleBin()
{
    const f32 positions[9] = {0, 0, 0, 1, 0, 0, 0, 1, 0};
    std::vector<u8> bin(sizeof(positions));
    std::memcpy(bin.data(), positions, sizeof(positions));
    return bin;
}

constexpr std::string_view kTriangleNodes =
    R"("asset":{"version":"2.0"},"scene":0,"scenes":[{"nodes":[0]}],"nodes":[{"mesh":0}],)"
    R"("meshes":[{"primitives":[{"attributes":{"POSITION":0},"material":0}]}],)"
    R"("accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3","min":[0,0,0],"max":[1,1,0]}],)";

/// GLB with one triangle and an embedded PNG used as base colour texture.
std::vector<u8> glbWithEmbeddedImage()
{
    std::vector<u8> bin = triangleBin(); // 36 bytes, already 4-aligned
    const std::vector<u8> image = png();
    bin.insert(bin.end(), image.begin(), image.end());
    while (bin.size() % 4 != 0)
    {
        bin.push_back(0);
    }
    std::string json =
        "{" + std::string(kTriangleNodes) +
        R"("materials":[{"name":"Holz","pbrMetallicRoughness":{"baseColorTexture":{"index":0}}}],)"
        R"("textures":[{"source":0}],"images":[{"bufferView":1,"mimeType":"image/png"}],)"
        R"("bufferViews":[{"buffer":0,"byteOffset":0,"byteLength":36},)" +
        R"({"buffer":0,"byteOffset":36,"byteLength":)" + std::to_string(image.size()) + "}]," +
        R"("buffers":[{"byteLength":)" + std::to_string(bin.size()) + "}]}";
    while (json.size() % 4 != 0)
    {
        json += ' ';
    }
    std::vector<u8> glb;
    put32(glb, 0x46546C67); // "glTF"
    put32(glb, 2);
    put32(glb, static_cast<u32>(12 + 8 + json.size() + 8 + bin.size()));
    put32(glb, static_cast<u32>(json.size()));
    put32(glb, 0x4E4F534A); // "JSON"
    glb.insert(glb.end(), json.begin(), json.end());
    put32(glb, static_cast<u32>(bin.size()));
    put32(glb, 0x004E4942); // "BIN\0"
    glb.insert(glb.end(), bin.begin(), bin.end());
    return glb;
}

/// .gltf with an external buffer and an external texture in a sibling directory.
std::string gltfWithExternalFiles(std::string_view imageUri)
{
    return "{" + std::string(kTriangleNodes) +
           R"("materials":[{"name":"Stein","pbrMetallicRoughness":{"baseColorTexture":{"index":0}}}],)"
           R"("textures":[{"source":0}],"images":[{"uri":")" +
           std::string(imageUri) +
           R"("}],)"
           R"("bufferViews":[{"buffer":0,"byteOffset":0,"byteLength":36}],"buffers":[{"byteLength":36,"uri":"house.bin"}]})";
}

void write(const fs::Path& path, std::span<const u8> data)
{
    std::filesystem::create_directories(path.parent_path());
    REQUIRE(fs::writeFile(path, data));
}

void writeText(const fs::Path& path, std::string_view text)
{
    std::filesystem::create_directories(path.parent_path());
    REQUIRE(fs::writeText(path, text));
}

/// assets/source-like tree.
struct SourceTree
{
    test::TempDir dir;
    fs::Path source = dir.path() / "source";
    fs::Path out = dir.path() / "cooked";

    SourceTree()
    {
        write(source / "models" / "Tri.glb", glbWithEmbeddedImage());
        write(source / "models" / "house.bin", triangleBin());
        writeText(source / "models" / "house.gltf", gltfWithExternalFiles("../textures/wood.png"));
        write(source / "textures" / "wood.png", png());
        writeText(source / "scripts" / "init.lua", "print('hello')");
        writeText(source / "models" / "hut.blend", "blender data");
        writeText(source / ".gitkeep", "");
        writeText(source / "textures" / ".DS_Store", "");
    }
};

std::vector<std::string> filesBelow(const fs::Path& root)
{
    std::vector<std::string> files;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(root))
    {
        if (entry.is_regular_file())
        {
            files.push_back(fs::toUtf8(entry.path().lexically_relative(root).generic_u8string()));
        }
    }
    std::sort(files.begin(), files.end());
    return files;
}
} // namespace

TEST_CASE("cook loose files: meshes, extracted and external images, copies, skips")
{
    SourceTree t;
    auto report = cook::cook({.source = t.source, .out = t.out});
    REQUIRE_MESSAGE(report, (report ? "" : report.error().message));
    const cook::CookReport& r = report.value();
    CHECK(r.errors.empty());
    CHECK(r.meshes == 2);
    CHECK(r.images == 2);  // wood.png + the image extracted from Tri.glb
    CHECK(r.copied == 1);  // init.lua
    CHECK(r.skipped == 4); // house.bin, hut.blend, .gitkeep, .DS_Store
    CHECK(r.outputs == 5);
    CHECK(filesBelow(t.out) == std::vector<std::string>{"models/Tri.g7mesh", "models/Tri.img0.png",
                                                        "models/house.g7mesh", "scripts/init.lua",
                                                        "textures/wood.png"});

    auto tri = asset::deserializeMesh(fs::readFile(t.out / "models" / "Tri.g7mesh").value());
    REQUIRE(tri);
    CHECK(tri.value().vertices.size() == 3);
    REQUIRE(tri.value().images.size() == 1);
    CHECK(tri.value().images[0].uri == "models/Tri.img0.png");
    CHECK(tri.value().images[0].encoded.empty());
    CHECK(fs::readFile(t.out / "models" / "Tri.img0.png").value() == png());

    auto house = asset::deserializeMesh(fs::readFile(t.out / "models" / "house.g7mesh").value());
    REQUIRE(house);
    REQUIRE(house.value().images.size() == 1);
    CHECK(house.value().images[0].uri == "textures/wood.png"); // VFS path, ".." resolved
    CHECK(house.value().materials[0].name == "Stein");
}

TEST_CASE("cook into an archive and load it through the AssetManager")
{
    SourceTree t;
    auto report = cook::cook({.source = t.source, .out = t.out, .pack = "data.g7pak"});
    REQUIRE(report);
    CHECK(report.value().outputs == 5);
    CHECK(filesBelow(t.out) == std::vector<std::string>{"data.g7pak"});

    asset::Vfs vfs;
    REQUIRE(vfs.mount(t.out / "data.g7pak", 0));
    asset::AssetManager assets(vfs);
    const auto mesh = assets.load<asset::MeshData>("models/tri.g7mesh");
    assets.waitAll();
    REQUIRE_MESSAGE(mesh.isReady(), mesh.error());
    const auto image = assets.load<asset::ImageData>(mesh->images[0].uri);
    const auto script = vfs.read("scripts/init.lua");
    assets.waitAll();
    REQUIRE(image.isReady());
    CHECK(image->width == 2);
    REQUIRE(script);
    CHECK(std::string(script.value().begin(), script.value().end()) == "print('hello')");

    // Same sources, same archive.
    const auto first = fs::readFile(t.out / "data.g7pak").value();
    REQUIRE(cook::cook({.source = t.source, .out = t.out, .pack = "data.g7pak"}));
    CHECK(fs::readFile(t.out / "data.g7pak").value() == first);
}

TEST_CASE("problems with single files are reported, the rest is cooked")
{
    SourceTree t;
    writeText(t.source / "models" / "broken.glb", "not a glb");
    writeText(t.source / "models" / "missing.gltf", gltfWithExternalFiles("nowhere.png"));
    writeText(t.source / "models" / "escape.gltf", gltfWithExternalFiles("../../outside.png"));
    writeText(t.source / "textures" / "broken.png", "not a png");
    write(t.source / "models" / "tri.gltf", glbWithEmbeddedImage()); // collides with Tri.glb -> tri.g7mesh

    auto report = cook::cook({.source = t.source, .out = t.out});
    REQUIRE(report);
    const auto& errors = report.value().errors;
    const auto mentions = [&](std::string_view needle)
    {
        return std::any_of(errors.begin(), errors.end(),
                           [&](const std::string& e) { return e.find(needle) != std::string::npos; });
    };
    CHECK(errors.size() == 5);
    CHECK(mentions("models/broken.glb"));
    CHECK(mentions("missing texture 'nowhere.png'"));
    CHECK(mentions("outside the source directory"));
    CHECK(mentions("textures/broken.png"));
    CHECK(mentions("collides"));
    CHECK(std::filesystem::exists(t.out / "models" / "house.g7mesh"));
    CHECK_FALSE(std::filesystem::exists(t.out / "models" / "missing.g7mesh"));
}

TEST_CASE("cook option checks and --clean")
{
    SourceTree t;
    CHECK_FALSE(cook::cook({.source = t.dir.path() / "nope", .out = t.out}));
    CHECK_FALSE(cook::cook({.source = t.source, .out = t.source / "cooked"}));
    CHECK_FALSE(cook::cook({.source = t.source, .out = t.dir.path(), .clean = true}));

    writeText(t.out / "stale.txt", "old");
    REQUIRE(cook::cook({.source = t.source, .out = t.out}));
    CHECK(std::filesystem::exists(t.out / "stale.txt")); // kept without --clean
    REQUIRE(cook::cook({.source = t.source, .out = t.out, .clean = true}));
    CHECK_FALSE(std::filesystem::exists(t.out / "stale.txt"));
    CHECK(std::filesystem::exists(t.source / "models" / "Tri.glb")); // sources untouched
}
