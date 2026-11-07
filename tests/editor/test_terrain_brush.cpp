// The height brush (M16 part F2, ADR 0023) on a small grid: raise, lower, smooth, flatten, paint; undo as the
// touched rectangle before and after (a height delta); the .r16 written as read.

#include <g7/editor/Commands.hpp>
#include <g7/editor/TerrainBrush.hpp>
#include <g7/world/Scene.hpp>
#include <g7/world/Terrain.hpp>

#include <doctest/doctest.h>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <ostream>
#include <string>
#include <vector>

using namespace g7;
using namespace g7::editor;

namespace
{
/// 21 x 21 samples, 1 m apart, from (0, 0); heights 0 .. 10 m, all at 2 m.
world::Heightfield field()
{
    world::TerrainRef ref;
    ref.heightmap = "test.r16";
    ref.width = 21;
    ref.height = 21;
    ref.cellSize = 1.0f;
    ref.firstSample = Vec2(0.0f);
    ref.minY = 0.0f;
    ref.maxY = 10.0f;
    auto f = world::Heightfield::create(ref, std::vector<u16>(21 * 21, static_cast<u16>(65535 / 5)));
    REQUIRE(f.ok());
    return std::move(f).value();
}
} // namespace

TEST_CASE("Terrain brush: falloff; raise and lower around the centre only")
{
    CHECK(brushFalloff(0.0f, 4.0f) == doctest::Approx(1.0f));
    CHECK(brushFalloff(2.0f, 4.0f) == doctest::Approx(0.5f));
    CHECK(brushFalloff(4.0f, 4.0f) == 0.0f);
    world::Heightfield f = field();
    const f32 start = f.sampleHeight(10, 10);
    Brush brush{BrushMode::Raise, 3.0f, 2.0f};
    applyHeightBrush(f, brush, Vec3(10, 0, 10), 0.5f); // 1 m at the centre
    CHECK(f.sampleHeight(10, 10) == doctest::Approx(start + 1.0f).epsilon(0.01));
    CHECK(f.sampleHeight(11, 10) > start);
    CHECK(f.sampleHeight(11, 10) < f.sampleHeight(10, 10));
    CHECK(f.sampleHeight(14, 10) == doctest::Approx(start)); // beyond the radius
    brush.mode = BrushMode::Lower;
    applyHeightBrush(f, brush, Vec3(10, 0, 10), 0.5f);
    CHECK(f.sampleHeight(10, 10) == doctest::Approx(start).epsilon(0.01));
    // Clamped to the block's range.
    brush.mode = BrushMode::Raise;
    applyHeightBrush(f, brush, Vec3(10, 0, 10), 100.0f);
    CHECK(f.sampleHeight(10, 10) == doctest::Approx(10.0f));
}

TEST_CASE("Terrain brush: smooth takes a spike down, flatten goes to the height")
{
    world::Heightfield f = field();
    f.setSampleHeight(10, 10, 8.0f);
    Brush smooth{BrushMode::Smooth, 2.0f, 1.0f};
    applyHeightBrush(f, smooth, Vec3(10, 0, 10), 1.0f);
    CHECK(f.sampleHeight(10, 10) < 8.0f);
    CHECK(f.sampleHeight(10, 10) > 2.0f);
    Brush flatten{BrushMode::Flatten, 3.0f, 100.0f, 5.0f};
    applyHeightBrush(f, flatten, Vec3(10, 0, 10), 1.0f);
    CHECK(f.sampleHeight(10, 10) == doctest::Approx(5.0f).epsilon(0.001));
    CHECK(f.sampleHeight(0, 0) == doctest::Approx(2.0f).epsilon(0.001));
}

TEST_CASE("Terrain brush: a stroke's rectangle before and after undoes and redoes exactly")
{
    world::Heightfield f = field();
    const std::vector<u16> start(f.samples().begin(), f.samples().end());
    const HeightPatch area = unite(brushArea(f, Vec3(5, 0, 5), 2.0f), brushArea(f, Vec3(15, 0, 6), 2.0f));
    CHECK(area.column == 3);
    CHECK(area.width == 15);
    Brush brush{BrushMode::Raise, 2.0f, 2.0f};
    applyHeightBrush(f, brush, Vec3(5, 0, 5), 0.5f);
    applyHeightBrush(f, brush, Vec3(15, 0, 6), 0.5f);
    const std::vector<u16> raised(f.samples().begin(), f.samples().end());

    Command command{"terrain: raise", {}, {}, false};
    command.heightsBefore = captureHeights(start, f.ref().width, area);
    command.heightsAfter = captureHeights(f, area);
    CHECK(command.heightsBefore->samples.size() == static_cast<usize>(area.width) * area.height); // a delta
    History history;
    history.record(std::move(command));
    world::Scene scene;
    TerrainTarget target{&f, {}};
    REQUIRE(history.undo(scene, nullptr, nullptr, nullptr, &target).value());
    CHECK(history.lastTouchedTerrain());
    CHECK(std::vector<u16>(f.samples().begin(), f.samples().end()) == start);
    REQUIRE(history.redo(scene, nullptr, nullptr, nullptr, &target).value());
    CHECK(std::vector<u16>(f.samples().begin(), f.samples().end()) == raised);
    // Outside the grid: nothing.
    CHECK(brushArea(f, Vec3(-50, 0, -50), 2.0f).width == 0);
}

TEST_CASE("Terrain brush: painting a layer keeps the weights' sum; splat patches undo")
{
    const world::Heightfield f = field();
    // Two maps of 21 x 21 pixels (8 layers): all weight on layer 0.
    std::vector<u8> a(21 * 21 * 4, 0);
    std::vector<u8> b(21 * 21 * 4, 0);
    for (usize i = 0; i < a.size(); i += 4)
    {
        a[i] = 255;
    }
    const std::vector<u8> before = a;
    std::vector<SplatPixels> maps{{a, 21, 21}, {b, 21, 21}};
    const auto areas = splatArea(maps, f.ref(), Vec3(10, 0, 10), 3.0f);
    REQUIRE(areas.size() == 2);
    std::vector<SplatPatch> undo;
    for (const SplatPatch& area : areas)
    {
        undo.push_back(
            captureSplat(area.map == 0 ? std::span<const u8>(a) : std::span<const u8>(b), 21, area));
    }
    Brush paint{BrushMode::Paint, 3.0f, 100.0f};
    paint.layer = 5; // map 1, channel 1
    paintSplat(maps, f.ref(), paint, Vec3(10, 0, 10), 1.0f);
    const usize centre = (10 * 21 + 10) * 4;
    CHECK(b[centre + 1] == 255);
    CHECK(a[centre] == 0);
    const usize edge = (10 * 21 + 12) * 4; // 2 m out: partly
    CHECK(static_cast<int>(a[edge]) + static_cast<int>(b[edge + 1]) == doctest::Approx(255).epsilon(0.02));
    CHECK(a[(0 * 21 + 0) * 4] == 255); // far away: untouched
    for (const SplatPatch& p : undo)
    {
        restoreSplat(maps, p);
    }
    CHECK(a == before);
    CHECK(std::ranges::all_of(b, [](u8 v) { return v == 0; }));
}

TEST_CASE("Terrain brush: the .r16 is written as it is read")
{
    world::Heightfield f = field();
    f.setSampleHeight(3, 4, 7.5f);
    const fs::Path file =
        std::filesystem::temp_directory_path() /
        ("g7_brush_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".r16");
    REQUIRE(f.saveR16(file).ok());
    auto bytes = fs::readFile(file);
    REQUIRE(bytes.ok());
    REQUIRE(bytes.value().size() == 21 * 21 * 2);
    const usize i = (4 * 21 + 3) * 2;
    const u16 v = static_cast<u16>(bytes.value()[i] | (bytes.value()[i + 1] << 8));
    CHECK(v == f.sample(3, 4));
    std::error_code ignored;
    std::filesystem::remove(file, ignored);
}
