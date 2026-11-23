// Grass fields (M17 part G1): qualities, the packed tuft, the tile builder (deterministic, density from the
// ground, no grass on holes, steep slopes or excluded ground) and the ring around the camera. CPU only.

#include <g7/core/FileSystem.hpp>
#include <g7/render/Grass.hpp>

#include <doctest/doctest.h>

#include <cmath>
#include <functional>
#include <vector>

using namespace g7;
using namespace g7::render;

namespace
{
/// Flat ground at y = 1: meadow (layer 0) for x < 0, cobbles (layer 1) for x >= 0, unless overridden.
struct FakeGround final : GrassGround
{
    std::function<f32(const Vec2&)> meadow = [](const Vec2& p) { return p.x < 0.0f ? 1.0f : 0.0f; };
    std::function<bool(const Vec2&)> hole = [](const Vec2&) { return false; };
    std::function<Vec3(const Vec2&)> normal = [](const Vec2&) { return Vec3(0.0f, 1.0f, 0.0f); };
    std::function<bool(const Vec3&)> blocked = [](const Vec3&) { return false; };
    f32 mask = 1.0f;
    mutable u32 samples = 0;
    u32 tilesBegun = 0;

    void beginTile(const Vec2&, const Vec2&) override { ++tilesBegun; }
    bool sample(const Vec2& xz, GrassGroundSample& out) const override
    {
        ++samples;
        if (hole(xz))
        {
            return false;
        }
        out.height = 1.0f;
        out.normal = normal(xz);
        out.weights[0] = meadow(xz);
        out.weights[1] = 1.0f - out.weights[0];
        out.mask = mask;
        return true;
    }
    bool excluded(const Vec3& foot) const override { return blocked(foot); }
};

std::vector<GrassLayer> layers()
{
    return {builtinGrassLayer("Wiese"), builtinGrassLayer("Kopfstein")};
}
} // namespace

TEST_CASE("Grass qualities: names, radius and density by the plan's table")
{
    CHECK(parseGrassQuality("off") == GrassQuality::Off);
    CHECK(parseGrassQuality("high") == GrassQuality::High);
    CHECK_FALSE(parseGrassQuality("ultra").has_value());
    CHECK(grassQualityName(GrassQuality::Medium) == "medium");
    CHECK(grassSettings(GrassQuality::Off).radius == 0.0f);
    CHECK(grassSettings(GrassQuality::Low).radius == 20.0f);
    CHECK(grassSettings(GrassQuality::Medium).tuftsPerSquareMetre == 3.0f);
    CHECK(grassSettings(GrassQuality::Medium).bladeDistance == 12.0f);
    CHECK(grassSettings(GrassQuality::High).radius == 50.0f);
    CHECK(grassSettings(GrassQuality::High).shadowDistance == 15.0f); // decision 8: shadows only on high
    CHECK(grassSettings(GrassQuality::Medium).shadowDistance == 0.0f);
    CHECK(builtinGrassLayer("Wiese").density == 1.0f);
    CHECK(builtinGrassLayer("grass").density == 1.0f); // the test camp
    CHECK(builtinGrassLayer("Kopfstein").density == 0.0f);
    CHECK(builtinGrassLayer("Acker").density == 0.0f);      // decision 7: bare earth for now
    CHECK(builtinGrassLayer("Waldboden").density == 0.25f); // G2 contract
    CHECK(builtinGrassLayer("Matsch").density == 0.1f);
}

TEST_CASE("Grass table: data/grass.toml holds the contract's values, errors name file and key")
{
    auto text = fs::readFile(fs::fromUtf8(G7_ASSET_SOURCE_DIR "/data/grass.toml"));
    REQUIRE(text);
    auto table = GrassTable::parse(
        std::string_view(reinterpret_cast<const char*>(text.value().data()), text.value().size()),
        "data/grass.toml");
    REQUIRE_MESSAGE(table, (table ? "" : table.error().message));
    const GrassTable builtin = GrassTable::builtin();
    for (const char* name :
         {"Wiese", "grass", "Waldboden", "Matsch", "Kopfstein", "Kies", "Acker", "Fels", "x"})
    {
        CAPTURE(name);
        const GrassLayer a = table.value().layer(name);
        const GrassLayer b = builtin.layer(name);
        CHECK(a.density == b.density);
        CHECK(a.height == b.height);
        CHECK(a.colourFoot == b.colourFoot);
        CHECK(a.colourTip == b.colourTip);
        CHECK(a.flowers == b.flowers);
    }
    CHECK(table.value().layer("grass").flowers); // the test camp's meadow is "Wiese" by another name

    const auto error = [](std::string_view toml)
    {
        auto t = GrassTable::parse(toml, "g.toml");
        REQUIRE_FALSE(t);
        return t.error().message;
    };
    CHECK(error("version = 2") == "g.toml: version: must be 1");
    CHECK(error("version = 1\n[layer.A]\ndensity = 2.0") == "g.toml: layer.A.density: must be within 0..1");
    CHECK(error("version = 1\n[layer.A]\nheight = [0.5]") ==
          "g.toml: layer.A.height: must be a list of 2 numbers");
    CHECK(error("version = 1\n[layer.A]\nheight = [0.5, 0.2]") ==
          "g.toml: layer.A.height: must be [min, max] with 0 < min <= max <= 1.2");
    CHECK(error("version = 1\n[layer.A]\nfoot = [1, 2]") ==
          "g.toml: layer.A.foot: must be a list of 3 numbers");
}

TEST_CASE("Grass tuft: the packed bits round trip")
{
    GrassTuftInfo info;
    info.yaw = 1.0f;
    info.height = 0.5f;
    info.layer = 5;
    info.tint = 3;
    info.kind = GrassKind::FlowerBlue;
    info.normalXz = Vec2(0.3f, -0.4f);
    const GrassTuftInfo back = unpackGrassTuft(packGrassTuft(info));
    CHECK(back.yaw == doctest::Approx(1.0f).epsilon(0.05));
    CHECK(back.height == doctest::Approx(0.5f).epsilon(0.03));
    CHECK(back.layer == 5);
    CHECK(back.tint == 3);
    CHECK(back.kind == GrassKind::FlowerBlue);
    CHECK(back.normalXz.x == doctest::Approx(0.3f).epsilon(0.1));
    CHECK(back.normalXz.y == doctest::Approx(-0.4f).epsilon(0.1));
    CHECK(grassTileAt(Vec2(-0.1f, 16.0f)) == GrassTileKey{-1, 1});
}

TEST_CASE("Grass tile: deterministic, only on the meadow, density by weight and mask")
{
    FakeGround ground;
    const auto l = layers();
    std::vector<GrassTuft> a;
    std::vector<GrassTuft> b;
    buildGrassTile(ground, l, {-1, 0}, 3.0f, a); // x -16..0: all meadow
    buildGrassTile(ground, l, {-1, 0}, 3.0f, b);
    REQUIRE(a.size() == b.size());
    for (usize i = 0; i < a.size(); ++i)
    {
        CHECK(a[i].position == b[i].position);
        CHECK(a[i].packed == b[i].packed);
    }
    // A full meadow keeps every candidate: 28 x 28 at 3 per m².
    CHECK(grassTileCapacity(3.0f) == 28u * 28u);
    CHECK(a.size() == grassTileCapacity(3.0f));
    for (const GrassTuft& t : a)
    {
        CHECK(t.position.x >= -16.0f);
        CHECK(t.position.x < 0.0f);
        CHECK(t.position.y == 1.0f);
        const GrassTuftInfo info = unpackGrassTuft(t.packed);
        CHECK(info.height >= 0.25f - 0.02f);
        CHECK(info.height <= 0.6f + 0.02f);
    }
    // Some flowers (decision 3: about 5 %).
    const auto flowers = std::ranges::count_if(
        a, [](const GrassTuft& t) { return unpackGrassTuft(t.packed).kind != GrassKind::Grass; });
    CHECK(flowers > 0);
    CHECK(static_cast<f32>(flowers) < 0.1f * static_cast<f32>(a.size()));
    // Cobbles: none.
    buildGrassTile(ground, l, {0, 0}, 3.0f, b);
    CHECK(b.empty());
    // A meadow share of 0.65 (blended with cobbles) half the tufts, 0.5 none (a path's edge); a mask of 0.25
    // a quarter.
    ground.meadow = [](const Vec2&) { return 0.5f; };
    buildGrassTile(ground, l, {3, 7}, 3.0f, b);
    CHECK(b.empty());
    ground.meadow = [](const Vec2&) { return 0.65f; };
    buildGrassTile(ground, l, {3, 7}, 3.0f, b);
    CHECK(static_cast<f32>(b.size()) == doctest::Approx(0.5f * static_cast<f32>(a.size())).epsilon(0.12));
    ground.meadow = [](const Vec2&) { return 1.0f; };
    ground.mask = 0.25f;
    buildGrassTile(ground, l, {3, 7}, 3.0f, b);
    CHECK(static_cast<f32>(b.size()) == doctest::Approx(0.25f * static_cast<f32>(a.size())).epsilon(0.2));
    // Another tile has another pattern.
    ground.mask = 1.0f;
    buildGrassTile(ground, l, {-2, 0}, 3.0f, b);
    CHECK(b.front().position.x != doctest::Approx(a.front().position.x - 16.0f));
}

TEST_CASE("Grass tile: no tufts on holes, slopes over 35 degrees or excluded ground")
{
    FakeGround ground;
    ground.meadow = [](const Vec2&) { return 1.0f; };
    const auto l = layers();
    std::vector<GrassTuft> tufts;
    ground.hole = [](const Vec2& p) { return p.x < 8.0f; };
    buildGrassTile(ground, l, {0, 0}, 3.0f, tufts);
    CHECK_FALSE(tufts.empty());
    CHECK(std::ranges::all_of(tufts, [](const GrassTuft& t) { return t.position.x >= 8.0f; }));
    ground.hole = [](const Vec2&) { return false; };
    // 40 degrees on the left half, 30 on the right.
    ground.normal = [](const Vec2& p)
    {
        const f32 a = glm::radians(p.x < 8.0f ? 40.0f : 30.0f);
        return Vec3(std::sin(a), std::cos(a), 0.0f);
    };
    buildGrassTile(ground, l, {0, 0}, 3.0f, tufts);
    CHECK_FALSE(tufts.empty());
    CHECK(std::ranges::all_of(tufts, [](const GrassTuft& t) { return t.position.x >= 8.0f; }));
    // The slope's normal goes along for the light.
    CHECK(unpackGrassTuft(tufts.front().packed).normalXz.x == doctest::Approx(0.5f).epsilon(0.1));
    ground.normal = [](const Vec2&) { return Vec3(0.0f, 1.0f, 0.0f); };
    // A path along z = 4 (1.2 m wide), say: the ground excludes it.
    ground.blocked = [](const Vec3& foot) { return std::abs(foot.z - 4.0f) < 0.6f; };
    buildGrassTile(ground, l, {0, 0}, 3.0f, tufts);
    CHECK_FALSE(tufts.empty());
    CHECK(
        std::ranges::none_of(tufts, [](const GrassTuft& t) { return std::abs(t.position.z - 4.0f) < 0.6f; }));
}

TEST_CASE("Grass ring: at most two tiles a frame, all within the radius in the end, far ones dropped")
{
    FakeGround ground;
    ground.meadow = [](const Vec2&) { return 1.0f; };
    const auto l = layers();
    GrassRing ring;
    const GrassSettings medium = grassSettings(GrassQuality::Medium);
    ring.reset(medium.radius, medium.tuftsPerSquareMetre);
    CHECK(ring.slotCount() == 49u);
    CHECK(ring.slotCapacity() == grassTileCapacity(3.0f));
    std::vector<u32> built;
    Vec3 eye(5.0f, 2.0f, 5.0f);
    u32 frames = 0;
    do
    {
        built.clear();
        ring.update(eye, ground, l, built);
        CHECK(built.size() <= kGrassTilesPerFrame);
        ++frames;
    } while (ring.pending() > 0 && frames < 100);
    CHECK(ring.pending() == 0);
    // Every tile whose square comes within 35 m: the nearest were built first.
    const usize tiles = ring.tiles().size();
    CHECK(tiles > 12);
    CHECK(tiles <= ring.slotCount());
    CHECK(frames == (tiles + 1) / 2);
    CHECK(ring.tiles().front().key == grassTileAt(Vec2(eye.x, eye.z)));
    CHECK(ring.tuftCount() == tiles * grassTileCapacity(3.0f));
    // Nothing more to do while the camera stands.
    built.clear();
    ring.update(eye, ground, l, built);
    CHECK(built.empty());
    // Moving 100 m: every old tile goes (beyond radius + margin), new ones come, slots are reused.
    eye.x += 100.0f;
    for (int i = 0; i < 100; ++i)
    {
        built.clear();
        ring.update(eye, ground, l, built);
    }
    CHECK(ring.pending() == 0);
    for (const GrassRing::Tile& t : ring.tiles())
    {
        const Vec2 min(static_cast<f32>(t.key.x) * kGrassTileSize,
                       static_cast<f32>(t.key.z) * kGrassTileSize);
        const Vec2 closest = glm::clamp(Vec2(eye.x, eye.z), min, min + Vec2(kGrassTileSize));
        CHECK(glm::length(closest - Vec2(eye.x, eye.z)) <= medium.radius + kGrassKeepMargin);
        CHECK(t.slot < ring.slotCount());
    }
    // Off: nothing.
    ring.reset(0.0f, 0.0f);
    built.clear();
    ring.update(eye, ground, l, built);
    CHECK(ring.tiles().empty());
    CHECK(built.empty());
}

TEST_CASE("Grass ring: the grass brush's rectangle makes tiles stale; rebuilt in place, four a frame")
{
    FakeGround ground;
    ground.meadow = [](const Vec2&) { return 1.0f; };
    const auto l = layers();
    GrassRing ring;
    const GrassSettings low = grassSettings(GrassQuality::Low);
    ring.reset(low.radius, low.tuftsPerSquareMetre);
    std::vector<u32> built;
    const Vec3 eye(5.0f, 2.0f, 5.0f);
    for (int i = 0; i < 100 && (ring.tiles().empty() || ring.pending() > 0); ++i)
    {
        ring.update(eye, ground, l, built);
    }
    REQUIRE(ring.pending() == 0);
    const usize tiles = ring.tiles().size();
    const u32 full = ring.tuftCount();
    // A rectangle inside tile (0, 0): only that tile.
    CHECK(ring.invalidate(Vec2(2.0f), Vec2(6.0f)) == 1u);
    CHECK(ring.staleCount() == 1u);
    const auto tile = [&](GrassTileKey key)
    { return *std::ranges::find(ring.tiles(), key, &GrassRing::Tile::key); };
    const u32 slot = tile({0, 0}).slot;
    CHECK(tile({0, 0}).stale);
    CHECK(tile({0, 0}).count > 0u); // shown until rebuilt
    // Now no grass there: rebuilt in its slot, nothing else.
    ground.mask = 0.0f;
    built.clear();
    ring.update(eye, ground, l, built);
    CHECK(built == std::vector<u32>{slot});
    CHECK(tile({0, 0}).slot == slot);
    CHECK_FALSE(tile({0, 0}).stale);
    CHECK(tile({0, 0}).count == 0u);
    CHECK(ring.tiles().size() == tiles);
    CHECK(ring.tuftCount() < full);
    // Crossing tile borders: every tile touching the rectangle; four a frame, the nearest first.
    CHECK(ring.invalidate(Vec2(-1.0f), Vec2(17.0f)) == 9u);
    built.clear();
    ring.update(eye, ground, l, built);
    CHECK(built.size() == kGrassStaleRebuildsPerFrame);
    CHECK(built.front() == slot); // (0, 0) holds the eye
    CHECK(ring.staleCount() == 5u);
    // Undo: all of them.
    CHECK(ring.invalidateAll() == tiles);
    for (int i = 0; i < 100 && ring.staleCount() > 0; ++i)
    {
        built.clear();
        ring.update(eye, ground, l, built);
    }
    CHECK(ring.staleCount() == 0u);
    CHECK(ring.tuftCount() == 0u);
}

TEST_CASE("Grass overlay: over the mask by its coverage, premultiplied")
{
    CHECK(grassMaskWithOverlay(0.8f, 0.0f, 0.0f) == doctest::Approx(0.8f)); // no overlay: the mask
    CHECK(grassMaskWithOverlay(0.8f, 0.0f, 1.0f) == doctest::Approx(0.0f)); // painted bare
    CHECK(grassMaskWithOverlay(0.0f, 1.0f, 1.0f) == doctest::Approx(1.0f)); // painted full
    CHECK(grassMaskWithOverlay(0.4f, 0.5f, 0.5f) == doctest::Approx(0.7f)); // full at half coverage
}

TEST_CASE("Grass makes way: bends away from figures, cut short around lying items (decisions 4, 5)")
{
    // Items: a third within 0.5 m, full height 0.15 m beyond.
    CHECK(grassItemScale(0.0f) == doctest::Approx(kGrassItemHeight));
    CHECK(grassItemScale(0.5f) == doctest::Approx(kGrassItemHeight));
    CHECK(grassItemScale(0.575f) == doctest::Approx(0.5f * (1.0f + kGrassItemHeight)));
    CHECK(grassItemScale(0.65f) == doctest::Approx(1.0f));
    // Figures: away from the feet, more the nearer, nothing beyond 0.8 m or far above.
    const Vec3 feet(10.0f, 2.0f, 10.0f);
    const Vec2 near = grassBendTip(Vec3(10.2f, 2.0f, 10.0f), 0.5f, feet);
    const Vec2 farther = grassBendTip(Vec3(10.6f, 2.0f, 10.0f), 0.5f, feet);
    CHECK(near.x > farther.x);
    CHECK(farther.x > 0.0f);
    CHECK(near.y == doctest::Approx(0.0f));
    CHECK(near.x < kGrassBendTip * 0.5f);
    CHECK(grassBendTip(Vec3(10.0f, 2.0f, 9.7f), 0.5f, feet).y < 0.0f); // -z of the feet: towards -z
    CHECK(glm::length(grassBendTip(Vec3(10.85f, 2.0f, 10.0f), 0.5f, feet)) == 0.0f);
    CHECK(glm::length(grassBendTip(Vec3(10.2f, 5.0f, 10.0f), 0.5f, feet)) == 0.0f); // a storey below
    // The nearest within the radius, at most max, nearest first.
    std::vector<Vec4> out;
    const std::vector<Vec3> points{Vec3(30, 0, 0), Vec3(5, 0, 0), Vec3(100, 0, 0), Vec3(1, 0, 0)};
    nearestGrassPoints(points, Vec3(0.0f), 35.0f, 2, 0.8f, out);
    REQUIRE(out.size() == 2);
    CHECK(out[0] == Vec4(1, 0, 0, 0.8f));
    CHECK(out[1] == Vec4(5, 0, 0, 0.8f));
    nearestGrassPoints(points, Vec3(0.0f), 35.0f, 16, 0.5f, out);
    CHECK(out.size() == 3);
}
