#include "../asset/TempDir.hpp"
#include "Cooker.hpp"
#include "Manifest.hpp"

#include <g7/asset/ImageData.hpp>

#include <doctest/doctest.h>

#include <chrono>
#include <cstring>
#include <filesystem>
#include <ostream> // doctest needs it to print std::string operands
#include <string>
#include <string_view>
#include <thread>
#include <vector>

using namespace g7;

namespace
{
std::vector<u8> png(u8 r, u8 g, u8 b)
{
    asset::ImageData img{4, 4, {}};
    for (int i = 0; i < 16; ++i)
    {
        img.rgba8.insert(img.rgba8.end(), {r, g, b, 255});
    }
    auto encoded = asset::encodePng(img);
    REQUIRE(encoded);
    return std::move(encoded).value();
}

std::vector<u8> bin(f32 x)
{
    const f32 positions[9] = {0, 0, 0, x, 0, 0, 0, 1, 0};
    std::vector<u8> out(sizeof(positions));
    std::memcpy(out.data(), positions, sizeof(positions));
    return out;
}

/// Triangle .gltf with external buffer; the material uses `textureUri` as base colour or normal map.
std::string gltf(std::string_view textureUri, bool asNormal = false)
{
    const std::string material =
        asNormal ? R"({"name":"M","normalTexture":{"index":0}})"
                 : R"({"name":"M","pbrMetallicRoughness":{"baseColorTexture":{"index":0}}})";
    return std::string(
               R"({"asset":{"version":"2.0"},"scene":0,"scenes":[{"nodes":[0]}],"nodes":[{"mesh":0}],)"
               R"("meshes":[{"primitives":[{"attributes":{"POSITION":0},"material":0}]}],)"
               R"("accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3","min":[0,0,0],"max":[1,1,0]}],)"
               R"("materials":[)") +
           material + R"(],"textures":[{"source":0}],"images":[{"uri":")" + std::string(textureUri) +
           R"("}],)" +
           R"("bufferViews":[{"buffer":0,"byteOffset":0,"byteLength":36}],"buffers":[{"byteLength":36,"uri":"wall.bin"}]})";
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

struct Tree
{
    test::TempDir dir;
    fs::Path source = dir.path() / "source";
    fs::Path out = dir.path() / "cooked";

    Tree()
    {
        write(source / "textures" / "wood.png", png(120, 80, 40));
        write(source / "models" / "wall.bin", bin(1.0f));
        writeText(source / "models" / "wall.gltf", gltf("../textures/wood.png"));
        writeText(source / "scripts" / "init.lua", "print('hi')");
    }

    cook::CookReport run(cook::CookOptions options = {})
    {
        options.source = source;
        options.out = out;
        auto report = cook::cook(options);
        REQUIRE_MESSAGE(report, (report ? "" : report.error().message));
        CHECK(report.value().errors.empty());
        return std::move(report).value();
    }

    std::filesystem::file_time_type mtime(std::string_view path) const
    {
        return std::filesystem::last_write_time(out / fs::fromUtf8(path));
    }
};
} // namespace

TEST_CASE("manifest: serialise and parse round trip, paths with spaces")
{
    cook::Manifest m;
    m.options = "textures=copy uastc=- output=loose";
    m.sources["models/my house.gltf"] = {0x0123456789abcdefull,
                                         {{"models/my house.bin", 42}, {"textures/a b.png", 0}},
                                         {{"models/my house.g7mesh", 7, 1234}}};
    m.sources["a.txt"] = {1, {}, {{"a.txt", 2, 0}}};
    const std::string text = cook::serializeManifest(m);
    auto parsed = cook::parseManifest(text);
    REQUIRE_MESSAGE(parsed, (parsed ? "" : parsed.error().message));
    CHECK(cook::serializeManifest(parsed.value()) == text);
    const auto& e = parsed.value().sources.at("models/my house.gltf");
    CHECK(e.key == 0x0123456789abcdefull);
    REQUIRE(e.dependencies.size() == 2);
    CHECK(e.dependencies[0].path == "models/my house.bin");
    CHECK(e.outputs[0].size == 1234);

    CHECK_FALSE(cook::parseManifest(""));
    CHECK_FALSE(cook::parseManifest("something else\n"));
    CHECK_FALSE(cook::parseManifest("g7cook-manifest\t1\nsource\tzz\tx\n"));
    CHECK_FALSE(cook::parseManifest("g7cook-manifest\t1\nbogus\n"));
}

TEST_CASE("gltfExternalUris: buffers and images, data URIs skipped, percent-decoded")
{
    const std::string json =
        R"({"buffers":[{"uri" : "wall.bin"},{"uri":"data:application/octet-stream;base64,AAAA"}],)"
        R"("images":[{"uri":"tex/my%20wood.png"}]})";
    const auto uris =
        cook::gltfExternalUris(std::span(reinterpret_cast<const u8*>(json.data()), json.size()));
    CHECK(uris == std::vector<std::string>{"wall.bin", "tex/my wood.png"});
}

TEST_CASE("second cook without changes reuses everything and rewrites nothing")
{
    Tree t;
    const auto first = t.run();
    CHECK(first.cooked == 3); // wood.png, wall.gltf, init.lua
    CHECK(first.reused == 0);
    const auto meshTime = t.mtime("models/wall.g7mesh");
    const auto bytes = fs::readFile(t.out / "models" / "wall.g7mesh").value();

    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    const auto second = t.run();
    CHECK(second.cooked == 0);
    CHECK(second.reused == 3);
    CHECK(second.outputs == 3);
    CHECK(t.mtime("models/wall.g7mesh") == meshTime); // not rewritten: hot reload stays quiet
    CHECK(fs::readFile(t.out / "models" / "wall.g7mesh").value() == bytes);
}

TEST_CASE("only changed sources and their dependants are cooked again")
{
    Tree t;
    t.run();

    SUBCASE("changed source")
    {
        writeText(t.source / "scripts" / "init.lua", "print('changed')");
        const auto r = t.run();
        CHECK(r.cooked == 1);
        CHECK(r.reused == 2);
        CHECK(fs::readText(t.out / "scripts" / "init.lua").value() == "print('changed')");
    }
    SUBCASE("changed glTF buffer recooks the mesh")
    {
        write(t.source / "models" / "wall.bin", bin(2.0f));
        const auto r = t.run();
        CHECK(r.cooked == 1);
        CHECK(r.meshes == 1);
    }
    SUBCASE("changed texture recooks the image and the mesh that refers to it")
    {
        write(t.source / "textures" / "wood.png", png(10, 200, 30));
        const auto r = t.run();
        CHECK(r.cooked == 2);
        CHECK(r.images == 1);
        CHECK(r.meshes == 1);
    }
    SUBCASE("output changed by hand is cooked again")
    {
        writeText(t.out / "scripts" / "init.lua", "tampered");
        const auto r = t.run();
        CHECK(r.cooked == 1);
        CHECK(fs::readText(t.out / "scripts" / "init.lua").value() == "print('hi')");
    }
    SUBCASE("other options cook everything")
    {
        const auto r = t.run({.full = true, .level = 1});
        CHECK(r.cooked == 3);
        CHECK(r.reused == 0);
    }
}

TEST_CASE("outputs of deleted sources are removed, unknown files stay")
{
    Tree t;
    t.run();
    writeText(t.out / "notes.txt", "not ours");
    std::filesystem::remove(t.source / "scripts" / "init.lua");
    const auto r = t.run();
    CHECK(r.reused == 2);
    CHECK_FALSE(std::filesystem::exists(t.out / "scripts" / "init.lua"));
    CHECK(std::filesystem::exists(t.out / "notes.txt"));
    const auto manifest = fs::readText(t.out / ".g7cook" / "manifest.txt").value();
    CHECK(manifest.find("init.lua") == std::string::npos);
}

TEST_CASE("broken or foreign manifest: everything is cooked")
{
    Tree t;
    t.run();
    writeText(t.out / ".g7cook" / "manifest.txt", "garbage");
    CHECK(t.run().cooked == 3);

    std::string text = fs::readText(t.out / ".g7cook" / "manifest.txt").value();
    text.replace(text.find("cooker\t"), 8, "cooker\t9");
    writeText(t.out / ".g7cook" / "manifest.txt", text);
    CHECK(t.run().cooked == 3);
}

TEST_CASE("pack mode: incremental archive equals a full cook")
{
    Tree t;
    t.run({.pack = "data.g7pak"});
    writeText(t.source / "scripts" / "init.lua", "print('v2')");
    const auto r = t.run({.pack = "data.g7pak"});
    CHECK(r.cooked == 1);
    CHECK(r.reused == 2);
    const auto incremental = fs::readFile(t.out / "data.g7pak").value();

    const auto full = t.run({.pack = "data.g7pak", .full = true});
    CHECK(full.cooked == 3);
    CHECK(fs::readFile(t.out / "data.g7pak").value() == incremental);
}

#if G7_HAS_KTX
TEST_CASE("KTX2: a texture that becomes a normal map is cooked again")
{
    Tree t;
    const cook::CookOptions ktx{.textures = cook::TextureMode::Ktx2, .uastcLevel = 0};
    t.run(ktx);
    CHECK(t.run(ktx).reused == 3);

    writeText(t.source / "models" / "wall.gltf", gltf("../textures/wood.png", true));
    const auto r = t.run(ktx);
    CHECK(r.cooked == 2); // the mesh (changed) and wood.png (now a normal map)
    CHECK(r.reused == 1);
}
#endif
