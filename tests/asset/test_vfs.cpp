#include "TempDir.hpp"

#include <g7/asset/Pak.hpp>
#include <g7/asset/Vfs.hpp>

#include <doctest/doctest.h>

#include <atomic>
#include <filesystem>
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

std::string text(const Result<std::vector<u8>>& r)
{
    REQUIRE(r);
    return {r.value().begin(), r.value().end()};
}

void writeText(const fs::Path& path, std::string_view content)
{
    std::filesystem::create_directories(path.parent_path());
    REQUIRE(fs::writeText(path, content));
}

/// base.g7pak (scripts, textures) plus a loose "mod" directory overriding one texture.
struct Fixture
{
    test::TempDir dir;
    fs::Path pak = dir.path() / "base.g7pak";
    fs::Path mod = dir.path() / "mod";

    Fixture()
    {
        PakWriter w;
        REQUIRE(w.add("textures/wood.png", bytes("base wood")));
        REQUIRE(w.add("textures/stone.png", bytes("base stone")));
        REQUIRE(w.add("scripts/Npc/Diego.lua", bytes("npc")));
        REQUIRE(w.add("scripts/items.lua", bytes("items")));
        REQUIRE(w.write(pak));
        writeText(mod / "Textures" / "Wood.png", "mod wood");
        writeText(mod / "readme.txt", "hello");
    }
};
} // namespace

TEST_CASE("higher priority overrides, unmount restores")
{
    Fixture f;
    Vfs vfs;
    REQUIRE(vfs.mount(f.pak, 0));
    CHECK(text(vfs.read("textures/wood.png")) == "base wood");

    auto mod = vfs.mount(f.mod, 10);
    REQUIRE(mod);
    CHECK(text(vfs.read("textures/wood.png")) == "mod wood");
    CHECK(text(vfs.read("textures/stone.png")) == "base stone"); // not overridden
    CHECK(vfs.stat("textures/wood.png")->path == "Textures/Wood.png");
    CHECK(vfs.diskPath("textures/wood.png") == f.mod / "Textures" / "Wood.png");

    CHECK(vfs.unmount(mod.value()));
    CHECK_FALSE(vfs.unmount(mod.value()));
    CHECK(text(vfs.read("textures/wood.png")) == "base wood");
    CHECK(vfs.mountCount() == 1);
}

TEST_CASE("lower priority does not override; equal priority: later mount wins")
{
    Fixture f;
    Vfs vfs;
    REQUIRE(vfs.mount(f.pak, 5));
    REQUIRE(vfs.mount(f.mod, 1));
    CHECK(text(vfs.read("textures/wood.png")) == "base wood");

    Vfs tie;
    REQUIRE(tie.mount(f.mod, 0));
    REQUIRE(tie.mount(f.pak, 0));
    CHECK(text(tie.read("textures/wood.png")) == "base wood");
    Vfs tieReversed;
    REQUIRE(tieReversed.mount(f.pak, 0));
    REQUIRE(tieReversed.mount(f.mod, 0));
    CHECK(text(tieReversed.read("textures/wood.png")) == "mod wood");
}

TEST_CASE("paths are case-insensitive and normalised on lookup")
{
    Fixture f;
    Vfs vfs;
    REQUIRE(vfs.mount(f.mod, 0));
    CHECK(vfs.exists("TEXTURES\\WOOD.PNG"));
    CHECK(vfs.exists("/textures//./wood.png"));
    CHECK_FALSE(vfs.exists("textures/../readme.txt"));
    CHECK_FALSE(vfs.exists("missing.txt"));
    auto missing = vfs.read("missing.txt");
    REQUIRE_FALSE(missing);
    CHECK(missing.error().message.find("missing.txt") != std::string::npos);
    CHECK_FALSE(vfs.read("../x"));
}

TEST_CASE("mount points prefix all files of a source")
{
    Fixture f;
    Vfs vfs;
    REQUIRE(vfs.mount(f.mod, 0, "Mods/Leonberg"));
    CHECK(vfs.exists("mods/leonberg/readme.txt"));
    CHECK_FALSE(vfs.exists("readme.txt"));
    CHECK(vfs.stat("mods/leonberg/readme.txt")->path == "Mods/Leonberg/readme.txt");
    CHECK_FALSE(vfs.mount(f.mod, 0, "../outside"));
}

TEST_CASE("list: recursive, filtered by directory and extension, sorted, winners only")
{
    Fixture f;
    Vfs vfs;
    REQUIRE(vfs.mount(f.pak, 0));
    REQUIRE(vfs.mount(f.mod, 10));

    std::vector<std::string> all;
    for (const auto& info : vfs.list())
    {
        all.push_back(info.path);
    }
    CHECK(all == std::vector<std::string>{"readme.txt", "scripts/items.lua", "scripts/Npc/Diego.lua",
                                          "textures/stone.png", "Textures/Wood.png"});

    const auto scripts = vfs.list("SCRIPTS", "LUA");
    REQUIRE(scripts.size() == 2);
    CHECK(scripts[0].path == "scripts/items.lua");
    CHECK(vfs.list("scripts/npc", ".lua").size() == 1);
    CHECK(vfs.list("textures", "png")[1].size == 8); // "mod wood" wins
    CHECK(vfs.list("scr").empty());                  // directory prefix, not string prefix
    CHECK(vfs.list("..").empty());
}

TEST_CASE("rescan picks up new and removed loose files")
{
    Fixture f;
    Vfs vfs;
    auto id = vfs.mount(f.mod, 0);
    REQUIRE(id);
    writeText(f.mod / "new.txt", "new");
    std::filesystem::remove(f.mod / "readme.txt");
    CHECK_FALSE(vfs.exists("new.txt")); // index is taken at mount time
    REQUIRE(vfs.rescan(id.value()));
    CHECK(vfs.exists("new.txt"));
    CHECK_FALSE(vfs.exists("readme.txt"));
    CHECK_FALSE(vfs.rescan(9999));
}

TEST_CASE("mount errors")
{
    test::TempDir dir;
    Vfs vfs;
    CHECK_FALSE(vfs.mount(dir.path() / "does-not-exist", 0));
    writeText(dir.path() / "not-a-pak.g7pak", "just text, not an archive");
    CHECK_FALSE(vfs.mount(dir.path() / "not-a-pak.g7pak", 0));
    CHECK(vfs.mountCount() == 0);
}

TEST_CASE("concurrent reads")
{
    Fixture f;
    Vfs vfs;
    REQUIRE(vfs.mount(f.pak, 0));
    REQUIRE(vfs.mount(f.mod, 10));
    std::atomic<int> failures{0};
    std::vector<std::thread> threads;
    for (int t = 0; t < 8; ++t)
    {
        threads.emplace_back(
            [&]
            {
                for (int i = 0; i < 200; ++i)
                {
                    const auto wood = vfs.read("textures/wood.png");
                    const auto npc = vfs.read("scripts/npc/diego.lua");
                    if (!wood || !npc || wood.value() != bytes("mod wood") || npc.value() != bytes("npc"))
                    {
                        ++failures;
                    }
                }
            });
    }
    for (auto& t : threads)
    {
        t.join();
    }
    CHECK(failures == 0);
}
