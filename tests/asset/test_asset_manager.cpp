#include "TempDir.hpp"

#include <g7/asset/AssetManager.hpp>
#include <g7/asset/ImageData.hpp>
#include <g7/asset/MeshData.hpp>
#include <g7/asset/Pak.hpp>
#include <g7/asset/Vfs.hpp>

#include <doctest/doctest.h>

#include <atomic>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <future>
#include <ostream> // doctest needs it to print std::string operands
#include <string>
#include <string_view>
#include <thread>
#include <vector>

using namespace g7;
using namespace g7::asset;

namespace
{
std::vector<u8> bytes(std::string_view s)
{
    return {s.begin(), s.end()};
}

void writeText(const fs::Path& path, std::string_view content)
{
    std::filesystem::create_directories(path.parent_path());
    REQUIRE(fs::writeText(path, content));
}

/// Test asset: the file contents as text.
struct TextAsset
{
    std::string text;
};

Result<TextAsset> loadText(const LoadContext& ctx)
{
    return TextAsset{std::string(ctx.bytes.begin(), ctx.bytes.end())};
}

/// A loose directory with a few text files, mounted into a Vfs.
struct Fixture
{
    test::TempDir dir;
    Vfs vfs;

    Fixture()
    {
        writeText(dir.path() / "data" / "a.txt", "alpha");
        writeText(dir.path() / "data" / "b.txt", "beta");
        writeText(dir.path() / "data" / "bad.txt", "");
        REQUIRE(vfs.mount(dir.path(), 0));
    }
};

/// 2 x 1 uncompressed 32-bit TGA, top-left origin: red, then semi-transparent blue.
std::vector<u8> tinyTga()
{
    std::vector<u8> tga = {0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2, 0, 1, 0, 32, 0x28};
    const u8 pixels[] = {0, 0, 255, 255, /* BGRA */ 255, 0, 0, 128};
    tga.insert(tga.end(), std::begin(pixels), std::end(pixels));
    return tga;
}

const std::vector<f32> kTriangle = {0, 0, 0, 1, 0, 0, 0, 1, 0};

std::string triangleGltf(std::string_view bufferUri)
{
    std::string uri = bufferUri.empty() ? "" : R"(,"uri":")" + std::string(bufferUri) + "\"";
    return R"({"asset":{"version":"2.0"},"scene":0,"scenes":[{"nodes":[0]}],"nodes":[{"mesh":0}],)"
           R"("meshes":[{"primitives":[{"attributes":{"POSITION":0}}]}],)"
           R"("accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3","min":[0,0,0],"max":[1,1,0]}],)"
           R"("bufferViews":[{"buffer":0,"byteOffset":0,"byteLength":36}],"buffers":[{"byteLength":36)" +
           uri + "}]}";
}

std::vector<u8> triangleBin()
{
    std::vector<u8> bin(kTriangle.size() * sizeof(f32));
    std::memcpy(bin.data(), kTriangle.data(), bin.size());
    return bin;
}

std::vector<u8> triangleGlb()
{
    std::string json = triangleGltf("");
    while (json.size() % 4 != 0)
    {
        json += ' ';
    }
    const std::vector<u8> bin = triangleBin();
    std::vector<u8> glb;
    const auto put32 = [&](u32 v)
    {
        for (int i = 0; i < 4; ++i)
        {
            glb.push_back(static_cast<u8>(v >> (8 * i)));
        }
    };
    put32(0x46546C67); // "glTF"
    put32(2);
    put32(12 + 8 + static_cast<u32>(json.size()) + 8 + static_cast<u32>(bin.size()));
    put32(static_cast<u32>(json.size()));
    put32(0x4E4F534A); // "JSON"
    glb.insert(glb.end(), json.begin(), json.end());
    put32(static_cast<u32>(bin.size()));
    put32(0x004E4942); // "BIN\0"
    glb.insert(glb.end(), bin.begin(), bin.end());
    return glb;
}
} // namespace

TEST_CASE("default handle is empty")
{
    const Handle<TextAsset> h;
    CHECK_FALSE(h.valid());
    CHECK(h.failed());
    CHECK(h.get() == nullptr);
    CHECK(h.path().empty());
    CHECK(h.version() == 0);
    CHECK(h.useCount() == 0);
}

TEST_CASE("synchronous mode: loads run in update(), state changes only there")
{
    Fixture f;
    AssetManager assets(f.vfs, {.workerThreads = 0});
    assets.registerLoader<TextAsset>(loadText);

    const auto a = assets.load<TextAsset>("data/a.txt");
    CHECK(a.valid());
    CHECK(a.state() == AssetState::Loading);
    CHECK(a.get() == nullptr);
    CHECK(assets.pendingCount() == 1);

    assets.update();
    REQUIRE(a.isReady());
    CHECK(a->text == "alpha");
    CHECK(a.path() == "data/a.txt");
    CHECK(a.version() == 1);
    CHECK(assets.pendingCount() == 0);
}

TEST_CASE("worker threads: finished loads appear only after update()")
{
    Fixture f;
    AssetManager assets(f.vfs, {.workerThreads = 2});
    std::promise<void> release;
    std::shared_future<void> gate = release.get_future().share();
    std::atomic<bool> loaderDone{false};
    assets.registerLoader<TextAsset>(
        [gate, &loaderDone](const LoadContext& ctx) -> Result<TextAsset>
        {
            gate.wait();
            auto result = loadText(ctx);
            loaderDone = true;
            return result;
        });

    const auto a = assets.load<TextAsset>("data/a.txt");
    assets.update();
    CHECK(a.state() == AssetState::Loading); // loader still blocked
    release.set_value();
    while (!loaderDone)
    {
        std::this_thread::yield();
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    CHECK(a.state() == AssetState::Loading); // finished, but not yet published
    assets.waitAll();
    REQUIRE(a.isReady());
    CHECK(a->text == "alpha");
}

TEST_CASE("same path and type share one asset; released after the last handle")
{
    Fixture f;
    AssetManager assets(f.vfs, {.workerThreads = 2});
    std::atomic<int> loads{0};
    assets.registerLoader<TextAsset>(
        [&loads](const LoadContext& ctx)
        {
            ++loads;
            return loadText(ctx);
        });

    auto a1 = assets.load<TextAsset>("data/a.txt");
    auto a2 = assets.load<TextAsset>("DATA\\A.TXT");
    CHECK(a1 == a2);
    assets.waitAll();
    CHECK(a1.useCount() == 2); // a running load also holds the asset until it is published
    CHECK(loads == 1);
    CHECK(a2->text == "alpha");
    CHECK(assets.cachedCount() == 1);

    // Another type for the same path is a different asset.
    auto image = assets.load<ImageData>("data/a.txt");
    CHECK(assets.cachedCount() == 2);
    assets.waitAll();
    CHECK(image.failed()); // "alpha" is no image

    a1 = {};
    a2 = {};
    image = {};
    assets.update();
    CHECK(assets.cachedCount() == 0);
    const auto again = assets.load<TextAsset>("data/a.txt");
    assets.waitAll();
    CHECK(again.isReady());
    CHECK(loads == 2); // loaded anew after release
}

TEST_CASE("failures end as Failed handles with a message")
{
    Fixture f;
    AssetManager assets(f.vfs, {.workerThreads = 1});
    assets.registerLoader<TextAsset>(
        [](const LoadContext& ctx) -> Result<TextAsset>
        {
            if (ctx.bytes.empty())
            {
                return Error{"empty file"};
            }
            return loadText(ctx);
        });

    const auto missing = assets.load<TextAsset>("data/missing.txt");
    const auto bad = assets.load<TextAsset>("data/bad.txt");
    assets.waitAll();
    REQUIRE(missing.failed());
    CHECK(missing.error().find("data/missing.txt") != std::string::npos);
    REQUIRE(bad.failed());
    CHECK(bad.error().find("empty file") != std::string::npos);
    CHECK(bad.get() == nullptr);

    // These fail at once, without update().
    struct Unregistered
    {
    };
    const auto noLoader = assets.load<Unregistered>("data/a.txt");
    CHECK(noLoader.failed());
    CHECK(noLoader.error().find("no loader") != std::string::npos);
    const auto invalid = assets.load<TextAsset>("../outside.txt");
    CHECK(invalid.failed());
    CHECK_FALSE(invalid.error().empty());
}

TEST_CASE("loaders read dependencies through the context")
{
    Fixture f;
    AssetManager assets(f.vfs, {.workerThreads = 0});
    assets.registerLoader<TextAsset>(
        [](const LoadContext& ctx) -> Result<TextAsset>
        {
            CHECK(ctx.sibling("b.txt") == "data/b.txt");
            CHECK(ctx.diskPath().has_value());
            auto other = ctx.read(ctx.sibling("b.txt"));
            if (!other)
            {
                return other.error();
            }
            return TextAsset{std::string(ctx.bytes.begin(), ctx.bytes.end()) + "+" +
                             std::string(other.value().begin(), other.value().end())};
        });
    const auto a = assets.load<TextAsset>("data/a.txt");
    assets.update();
    REQUIRE(a.isReady());
    CHECK(a->text == "alpha+beta");

    const LoadContext root{"top.txt", {}, &f.vfs};
    CHECK(root.sibling("x.txt") == "x.txt");
}

TEST_CASE("many parallel loads")
{
    test::TempDir dir;
    for (int i = 0; i < 100; ++i)
    {
        writeText(dir.path() / ("f" + std::to_string(i) + ".txt"), std::to_string(i * i));
    }
    Vfs vfs;
    REQUIRE(vfs.mount(dir.path(), 0));
    AssetManager assets(vfs, {.workerThreads = 4});
    assets.registerLoader<TextAsset>(loadText);

    std::vector<Handle<TextAsset>> handles;
    for (int i = 0; i < 100; ++i)
    {
        handles.push_back(assets.load<TextAsset>("f" + std::to_string(i) + ".txt"));
    }
    assets.waitAll();
    CHECK(assets.pendingCount() == 0);
    for (int i = 0; i < 100; ++i)
    {
        REQUIRE(handles[static_cast<usize>(i)].isReady());
        CHECK(handles[static_cast<usize>(i)]->text == std::to_string(i * i));
    }
}

TEST_CASE("destroying the manager fails unpublished loads and does not hang")
{
    Fixture f;
    std::vector<Handle<TextAsset>> handles;
    {
        AssetManager assets(f.vfs, {.workerThreads = 1});
        std::promise<void> release;
        std::shared_future<void> gate = release.get_future().share();
        assets.registerLoader<TextAsset>(
            [gate](const LoadContext& ctx)
            {
                gate.wait();
                return loadText(ctx);
            });
        handles.push_back(assets.load<TextAsset>("data/a.txt"));
        handles.push_back(assets.load<TextAsset>("data/b.txt"));
        release.set_value();
    } // destructor joins the worker
    for (const auto& h : handles)
    {
        REQUIRE(h.failed());
        CHECK(h.error().find("shut down") != std::string::npos);
    }
}

TEST_CASE("built-in loaders: ImageData and MeshData")
{
    test::TempDir dir;
    const fs::Path loose = dir.path() / "loose";
    REQUIRE(fs::createDirectories(loose));
    REQUIRE(fs::writeFile(loose / "tri.bin", triangleBin()));
    writeText(loose / "tri.gltf", triangleGltf("tri.bin"));

    PakWriter pak;
    REQUIRE(pak.add("textures/tiny.tga", tinyTga()));
    REQUIRE(pak.add("meshes/tri.glb", triangleGlb()));
    REQUIRE(pak.add("meshes/external.gltf", bytes(triangleGltf("tri.bin"))));
    REQUIRE(pak.write(dir.path() / "base.g7pak"));

    Vfs vfs;
    REQUIRE(vfs.mount(dir.path() / "base.g7pak", 0));
    REQUIRE(vfs.mount(loose, 0, "loose"));
    AssetManager assets(vfs);

    const auto image = assets.load<ImageData>("textures/tiny.tga");
    const auto glb = assets.load<MeshData>("meshes/tri.glb");
    const auto gltf = assets.load<MeshData>("loose/tri.gltf");
    const auto archived = assets.load<MeshData>("meshes/external.gltf");
    assets.waitAll();

    REQUIRE(image.isReady());
    CHECK(image->width == 2);
    CHECK(image->height == 1);
    CHECK(image->rgba8 == std::vector<u8>{255, 0, 0, 255, 0, 0, 255, 128});
    REQUIRE_MESSAGE(glb.isReady(), glb.error());
    CHECK(glb->vertices.size() == 3);
    REQUIRE_MESSAGE(gltf.isReady(), gltf.error()); // external .bin next to a loose file
    CHECK(gltf->vertices[2].position == Vec3(0, 1, 0));
    REQUIRE(archived.failed());
    CHECK(archived.error().find(".glb") != std::string::npos);
}
