#include <g7/asset/Vfs.hpp>
#include <g7/render/Terrain.hpp>
#include <g7/world/Terrain.hpp>
#include <g7/world/WorldFile.hpp>

#include <doctest/doctest.h>

#include <chrono>
#include <filesystem>
#include <ostream>
#include <string>

using namespace g7;
using namespace g7::world;

namespace
{
/// 3 x 2 samples, 2 m apart, first sample at (10, 20), heights 0..100 m:
///   row 0 (z = 20): 0, 10, 20 m     row 1 (z = 22): 30, 40, 50 m
Heightfield smallField()
{
    TerrainRef ref{"t.r16", 3, 2, 2.0f, Vec2(10.0f, 20.0f), 0.0f, 100.0f};
    std::vector<u16> samples;
    for (const f32 h : {0.0f, 10.0f, 20.0f, 30.0f, 40.0f, 50.0f})
    {
        samples.push_back(encodeHeight(h, 0.0f, 100.0f));
    }
    auto field = Heightfield::create(ref, samples);
    REQUIRE(field.ok());
    return std::move(field).value();
}
} // namespace

TEST_CASE("Terrain: height encoding as in the contract (worked examples)")
{
    // docs/modules/world.md: minY -50.991, maxY 94.85, step 2.2254 mm.
    const f32 minY = -50.991f;
    const f32 maxY = 94.85f;
    CHECK(encodeHeight(0.0f, minY, maxY) == 22913);
    CHECK(decodeHeight(22913, minY, maxY) == doctest::Approx(-0.000616f).epsilon(0.02));
    CHECK(encodeHeight(21.9f, minY, maxY) == 32754);
    CHECK(decodeHeight(32754, minY, maxY) == doctest::Approx(21.899457f));
    CHECK(encodeHeight(minY, minY, maxY) == 0);
    CHECK(encodeHeight(maxY, minY, maxY) == 65535);
    CHECK(encodeHeight(-100.0f, minY, maxY) == 0); // clamped
    CHECK(encodeHeight(500.0f, minY, maxY) == 65535);
    CHECK(decodeHeight(65535, minY, maxY) == doctest::Approx(maxY));
}

TEST_CASE("Terrain: heights at sample centres, bilinear between, edge outside")
{
    const Heightfield field = smallField();
    // Sample (c, r) at x = 10 + 2c, z = 20 + 2r.
    CHECK(field.heightAt(10.0f, 20.0f) == doctest::Approx(0.0f).epsilon(0.001));
    CHECK(field.heightAt(14.0f, 20.0f) == doctest::Approx(20.0f).epsilon(0.001));
    CHECK(field.heightAt(12.0f, 22.0f) == doctest::Approx(40.0f).epsilon(0.001));
    CHECK(field.heightAt(11.0f, 21.0f) == doctest::Approx(20.0f).epsilon(0.001)); // centre of 0, 10, 30, 40
    CHECK(field.heightAt(-50.0f, 20.0f) == doctest::Approx(0.0f).epsilon(0.001)); // west of the area
    CHECK(field.heightAt(99.0f, 99.0f) == doctest::Approx(50.0f).epsilon(0.001)); // south-east corner
    CHECK(field.sampleHeight(2, 1) == doctest::Approx(50.0f).epsilon(0.001));
    CHECK(field.sampleHeight(-3, 7) == doctest::Approx(30.0f).epsilon(0.001)); // clamped to (0, 1)

    const AABB bounds = field.bounds();
    CHECK(bounds.min == Vec3(10.0f, 0.0f, 20.0f));
    CHECK(bounds.max == Vec3(14.0f, 100.0f, 22.0f));

    // Rising 5 m per metre to +X and 15 m per metre to +Z: the normal leans to -X, -Z.
    const Vec3 n = field.normalAt(12.0f, 21.0f);
    CHECK(n.x < 0.0f);
    CHECK(n.z < n.x);
    CHECK(glm::length(n) == doctest::Approx(1.0f));

    const render::HeightfieldDesc desc = field.renderDesc();
    CHECK(desc.width == 3);
    CHECK(desc.samples.size() == 6);
    CHECK(desc.firstSample == Vec2(10.0f, 20.0f));
}

TEST_CASE("Terrain: invalid blocks and data are errors")
{
    const TerrainRef good{"t.r16", 3, 2, 1.0f, Vec2(0.0f), 0.0f, 1.0f};
    CHECK(Heightfield::create(good, std::vector<u16>(6)).ok());
    CHECK_FALSE(Heightfield::create(good, std::vector<u16>(5)).ok()); // sample count
    TerrainRef bad = good;
    bad.width = 1;
    CHECK_FALSE(Heightfield::create(bad, std::vector<u16>(2)).ok());
    bad = good;
    bad.cellSize = 0.0f;
    CHECK_FALSE(Heightfield::create(bad, std::vector<u16>(6)).ok());
    bad = good;
    bad.maxY = bad.minY;
    CHECK_FALSE(Heightfield::create(bad, std::vector<u16>(6)).ok());
}

TEST_CASE("Terrain: loading the .r16 through the VFS, little endian")
{
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    const fs::Path dir = std::filesystem::temp_directory_path() / ("g7_terrain_" + std::to_string(stamp));
    std::filesystem::create_directories(dir);
    // 2 x 2: 0x0102, 0x0304, 0xFFFF, 0x0000 as little-endian bytes.
    REQUIRE(
        fs::writeFile(dir / "t.r16", std::vector<u8>{0x02, 0x01, 0x04, 0x03, 0xFF, 0xFF, 0x00, 0x00}).ok());
    REQUIRE(fs::writeFile(dir / "odd.r16", std::vector<u8>{1, 2, 3}).ok());
    asset::Vfs vfs;
    REQUIRE(vfs.mount(dir, 0).ok());
    auto field = Heightfield::load(vfs, {"t.r16", 2, 2, 1.0f, Vec2(0.0f), 0.0f, 65535.0f});
    REQUIRE_MESSAGE(field.ok(), (field.ok() ? "" : field.error().message));
    CHECK(field.value().sampleHeight(0, 0) == doctest::Approx(258.0f));
    CHECK(field.value().sampleHeight(1, 0) == doctest::Approx(772.0f));
    CHECK(field.value().sampleHeight(0, 1) == doctest::Approx(65535.0f));
    CHECK_FALSE(Heightfield::load(vfs, {"odd.r16", 2, 2, 1.0f, Vec2(0.0f), 0.0f, 1.0f}).ok());
    CHECK_FALSE(Heightfield::load(vfs, {"missing.r16", 2, 2, 1.0f, Vec2(0.0f), 0.0f, 1.0f}).ok());
    std::error_code ignored;
    std::filesystem::remove_all(dir, ignored);
}

TEST_CASE("Terrain: the .g7world block is optional, versioned, read and written")
{
    const std::string text = R"({"version": 1, "name": "w",
      "terrain": {"version": 1, "heightmap": "worlds/leonberg/terrain.r16", "width": 2000, "height": 2000,
                  "cellSize": 1.0, "firstSample": [-999.5, -999.5], "minY": -50.991, "maxY": 94.85,
                  "splat": ["reserved"]},
      "vobs": []})";
    auto world = parseWorldFile(text, "w.g7world");
    REQUIRE_MESSAGE(world.ok(), (world.ok() ? "" : world.error().message));
    REQUIRE(world.value().terrain.has_value());
    const TerrainRef& t = *world.value().terrain;
    CHECK(t.heightmap == "worlds/leonberg/terrain.r16");
    CHECK(t.width == 2000);
    CHECK(t.firstSample == Vec2(-999.5f, -999.5f));
    CHECK(t.minY == doctest::Approx(-50.991f));
    // Written back on one line and read again unchanged.
    const std::string written = writeWorldFile(world.value());
    CHECK(written.find("\"terrain\": {\"version\":1,\"heightmap\":\"worlds/leonberg/terrain.r16\"") !=
          std::string::npos);
    CHECK(writeWorldFile(parseWorldFile(written).value()) == written);

    CHECK_FALSE(parseWorldFile(R"({"version": 1})").value().terrain.has_value()); // v1 without terrain
    const auto error = [](std::string_view block)
    {
        auto w = parseWorldFile(std::string(R"({"version": 1, "terrain": )") + std::string(block) + "}",
                                "w.g7world");
        REQUIRE_FALSE(w.ok());
        return w.error().message;
    };
    CHECK(error(R"({"heightmap": "a.r16"})") == "w.g7world: terrain: needs a 'version'");
    CHECK(error(R"({"version": 2})") == "w.g7world: terrain: version 2 is not supported (expected 1)");
    CHECK(error(R"({"version": 1, "width": 4, "height": 4})") ==
          "w.g7world: terrain: needs 'heightmap' (VFS path)");
    CHECK(error(R"({"version": 1, "heightmap": "a.r16", "width": 1, "height": 4})") ==
          "w.g7world: terrain: 'width' must be an integer >= 2");
    CHECK(
        error(
            R"({"version": 1, "heightmap": "a.r16", "width": 4, "height": 4, "cellSize": 1, "minY": 5, "maxY": 5,
                    "firstSample": [0, 0]})") == "w.g7world: terrain: 'maxY' must be above 'minY'");
    CHECK(
        error(
            R"({"version": 1, "heightmap": "a.r16", "width": 4, "height": 4, "cellSize": 1, "minY": 0, "maxY": 5,
                    "firstSample": [0]})") == "w.g7world: terrain.firstSample: must be a list of 2 numbers");
}

TEST_CASE("Terrain: detail levels by distance")
{
    using render::TerrainRenderer;
    CHECK(TerrainRenderer::lodFor(0.0f, 100.0f) == 0);
    CHECK(TerrainRenderer::lodFor(99.0f, 100.0f) == 0);
    CHECK(TerrainRenderer::lodFor(100.0f, 100.0f) == 1);
    CHECK(TerrainRenderer::lodFor(250.0f, 100.0f) == 2);
    CHECK(TerrainRenderer::lodFor(450.0f, 100.0f) == 3);
    CHECK(TerrainRenderer::lodFor(1e6f, 100.0f) == TerrainRenderer::kLodLevels - 1);
}
