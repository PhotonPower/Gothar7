#include <g7/asset/Vfs.hpp>
#include <g7/runtime/AssetMounts.hpp>

#include <doctest/doctest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <ostream>
#include <string>

using namespace g7;

namespace
{
/// Temporary folder tree, removed afterwards.
struct TempTree
{
    fs::Path root;
    TempTree()
    {
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        root = std::filesystem::temp_directory_path() / ("g7_mounts_" + std::to_string(stamp));
        std::filesystem::create_directories(root);
    }
    ~TempTree()
    {
        std::error_code ignored;
        std::filesystem::remove_all(root, ignored);
    }
    void dir(std::string_view path) const { std::filesystem::create_directories(root / fs::fromUtf8(path)); }
    void file(std::string_view path) const { std::ofstream(root / fs::fromUtf8(path)) << "x"; }
};

Config parse(std::string_view toml)
{
    auto config = Config::parse(toml);
    REQUIRE(config.ok());
    return std::move(config).value();
}
} // namespace

TEST_CASE("Asset mounts: development folders")
{
    TempTree tree;
    tree.dir("assets/source");
    const fs::Path devRoot = tree.root / "assets";

    // Only folders that exist: no cooked data yet.
    auto mounts = assetMounts(parse(""), tree.root, devRoot);
    REQUIRE(mounts.ok());
    REQUIRE(mounts.value().size() == 1);
    CHECK(mounts.value()[0].source == devRoot / "source");
    CHECK(mounts.value()[0].priority == kDevSourcePriority);

    tree.dir("assets/cooked");
    mounts = assetMounts(parse(""), tree.root, devRoot);
    REQUIRE(mounts.value().size() == 2);
    CHECK(mounts.value()[1].source == devRoot / "cooked");
    CHECK(mounts.value()[1].priority > mounts.value()[0].priority); // cooked wins

    // The cooker's archive: a folder mount would only show it as a file, so it is mounted itself,
    // above loose cooked files.
    tree.file("assets/cooked/data.g7pak");
    mounts = assetMounts(parse(""), tree.root, devRoot);
    REQUIRE(mounts.value().size() == 3);
    CHECK(mounts.value()[2].source.filename() == "data.g7pak");
    CHECK(mounts.value()[2].priority == kDevCookedArchivePriority);
    CHECK(kDevCookedArchivePriority > kDevCookedPriority);

    CHECK(assetMounts(parse("[assets]\ndev_mounts = false\n"), tree.root, devRoot).value().empty());
    CHECK(assetMounts(parse(""), tree.root, fs::Path{}).value().empty()); // shipping build: no dev root
}

TEST_CASE("Asset mounts: configured folders and archive patterns")
{
    TempTree tree;
    tree.dir("data");
    tree.file("data/patch_02.g7pak");
    tree.file("data/base.G7PAK");
    tree.file("data/patch_01.g7pak");
    tree.file("data/readme.txt");
    auto mounts = assetMounts(parse(R"(
[[assets.mount]]
path = "data/*.g7pak"
priority = 5

[[assets.mount]]
path = "mods/castle"
priority = 20
mount_point = "worlds"
)"),
                              tree.root, fs::Path{});
    REQUIRE(mounts.ok());
    REQUIRE(mounts.value().size() == 4);
    // Archives in name order (later names win on equal priority), relative to the game folder.
    CHECK(mounts.value()[0].source.filename() == "base.G7PAK");
    CHECK(mounts.value()[1].source.filename() == "patch_01.g7pak");
    CHECK(mounts.value()[2].source.filename() == "patch_02.g7pak");
    CHECK(mounts.value()[2].priority == 5);
    // Plain folders are listed even if missing; mounting reports them.
    CHECK(mounts.value()[3].source == (tree.root / "mods" / "castle").lexically_normal());
    CHECK(mounts.value()[3].mountPoint == "worlds");

    const auto error = [&](std::string_view toml)
    {
        auto result = assetMounts(parse(toml), tree.root, fs::Path{});
        REQUIRE_FALSE(result.ok());
        return result.error().message;
    };
    CHECK(error("[[assets.mount]]\npriority = 1\n") == "[assets] mount 0 needs a 'path'");
    CHECK(error("[[assets.mount]]\npath = \"x\"\npriority = 1.5\n") ==
          "[assets] mount 0: 'priority' must be an integer");
}

TEST_CASE("Asset mounts: cooked meshes and textures replace their sources")
{
    TempTree tree;
    tree.dir("models");
    tree.file("models/hut.glb");
    tree.file("models/hut.g7mesh");
    tree.file("models/tree.gltf");
    tree.dir("worlds");
    tree.file("worlds/splat0.png");
    tree.file("worlds/splat0.ktx2");
    tree.file("worlds/rock.jpg");
    asset::Vfs vfs;
    REQUIRE(vfs.mount(tree.root, 0).ok());
    CHECK(preferCooked(vfs, "models/hut.glb") == "models/hut.g7mesh");
    CHECK(preferCooked(vfs, "models/HUT.GLB") == "models/HUT.g7mesh");  // the VFS matches case-insensitively
    CHECK(preferCooked(vfs, "models/tree.gltf") == "models/tree.gltf"); // not cooked yet
    CHECK(preferCooked(vfs, "models/hut.g7mesh") == "models/hut.g7mesh");
    CHECK(preferCooked(vfs, "textures/wood.png") == "textures/wood.png");
    CHECK(preferCooked(vfs, "worlds/splat0.png") == "worlds/splat0.ktx2"); // terrain blocks name the PNG
    CHECK(preferCooked(vfs, "worlds/rock.jpg") == "worlds/rock.jpg");
}
