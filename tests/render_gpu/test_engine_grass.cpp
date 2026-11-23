// Grass in the engine (M17 part G1): around the camera the ring fills (two tiles a frame) and no tuft stands
// on the waynet's paths, in water, in a room or under a house or mob (the collision above it).

#include "GlFixture.hpp"

#include <g7/core/Config.hpp>
#include <g7/runtime/Engine.hpp>

#include <doctest/doctest.h>

#include <filesystem>
#include <string>

using namespace g7;

namespace
{
std::unique_ptr<Engine> grassEngine(const char* world, const Vec3& camera, std::string_view quality)
{
    auto settings = Config::parse(std::format("[render]\ngrass = \"{}\"\n", quality));
    REQUIRE(settings.ok());
    EngineConfig config;
    config.appName = "grass";
    config.window.size = {320, 180};
    config.window.vsync = false;
    config.world = fs::fromUtf8(world);
    config.startTime = "12:00";
    config.shaderDirectory = fs::fromUtf8(G7_SHADER_DIR);
    config.fixedFrameSeconds = 1.0 / 60.0;
    config.player = false;
    config.settings = std::move(settings).value();
    config.view.camera = camera;
    config.view.pitchDegrees = -10.0f;
    config.view.fly = true;
    test::keepVideoAlive();
    auto engine = std::make_unique<Engine>(config);
    REQUIRE(engine->init().ok());
    REQUIRE(engine->renderDevice() != nullptr);
    return engine;
}

f32 segmentDistanceXz(const Vec3& p, const Vec3& a, const Vec3& b)
{
    const Vec2 ab(b.x - a.x, b.z - a.z);
    const Vec2 ap(p.x - a.x, p.z - a.z);
    const f32 t =
        glm::dot(ab, ab) > 0.0f ? std::clamp(glm::dot(ap, ab) / glm::dot(ab, ab), 0.0f, 1.0f) : 0.0f;
    return glm::length(ap - ab * t);
}

/// Every tuft of the ring checked against the exclusions; returns how many there are.
usize checkTufts(const Engine& engine)
{
    const auto& points = engine.waynet().points();
    std::vector<render::IndoorVolume> rooms;
    for (const world::Zone& z : engine.worldFile().zones)
    {
        if (z.box && z.type == "indoor")
        {
            rooms.push_back({z.box->center, z.box->halfExtents, glm::radians(z.box->yawDegrees)});
        }
    }
    usize count = 0;
    usize bad = 0;
    for (const render::GrassRing::Tile& tile : engine.grassRing().tiles())
    {
        for (const render::GrassTuft& t : engine.grassRing().tufts(tile.slot))
        {
            ++count;
            const Vec3& foot = t.position;
            bool wrong = false;
            for (u32 i = 0; i < points.size() && !wrong; ++i)
            {
                for (const u32 j : engine.waynet().neighbours(i))
                {
                    wrong = wrong || segmentDistanceXz(foot, points[i].position, points[j].position) < 0.59f;
                }
            }
            wrong = wrong || engine.water().submergedIn(foot + Vec3(0.0f, 0.02f, 0.0f)) != nullptr;
            wrong = wrong || (!rooms.empty() &&
                              render::indoorAmount(foot + Vec3(0.0f, 0.1f, 0.0f), rooms, 0.0f) > 0.5f);
            const auto hit = engine.physics().raycast(
                foot + Vec3(0.0f, 2.0f, 0.0f), Vec3(0.0f, -1.0f, 0.0f), 1.94f,
                physics::layerBit(physics::Layer::World) | physics::layerBit(physics::Layer::Mob));
            wrong = wrong || (hit && hit->userData != 0);
            if (wrong && bad++ < 5)
            {
                MESSAGE("tuft at ", foot.x, ", ", foot.y, ", ", foot.z, " where no grass may grow: water ",
                        engine.water().submergedIn(foot + Vec3(0.0f, 0.02f, 0.0f)) != nullptr, " hit ",
                        hit ? hit->position.y : -99.0f, " user ", hit ? hit->userData : 0);
            }
        }
    }
    CHECK(bad == 0);
    return count;
}
} // namespace

TEST_CASE("Engine grass GPU: the camp's meadow fills two tiles a frame, none on paths, water or under huts")
{
    auto engine = grassEngine("testworld/camp.g7world", Vec3(10.0f, 4.0f, 12.0f), "medium");
    CHECK(engine->grassQuality() == render::GrassQuality::Medium);
    usize previous = 0;
    for (int i = 0; i < 60; ++i)
    {
        REQUIRE(engine->runFrame());
        CHECK(engine->grassRing().tiles().size() <= previous + render::kGrassTilesPerFrame);
        previous = engine->grassRing().tiles().size();
    }
    CHECK(engine->grassRing().pending() == 0);
    const usize tufts = checkTufts(*engine);
    MESSAGE(engine->grassRing().tiles().size(), " tiles, ", tufts, " tufts, drawn near ",
            engine->grassStats().nearTufts, " far ", engine->grassStats().farTufts);
    CHECK(tufts > 1000);
    CHECK(engine->grassStats().nearTufts + engine->grassStats().farTufts > 0);
    CHECK(engine->renderDevice()->debugErrorCount() == 0);
}

TEST_CASE("Engine grass GPU: off draws nothing")
{
    auto engine = grassEngine("testworld/camp.g7world", Vec3(10.0f, 4.0f, 12.0f), "off");
    for (int i = 0; i < 5; ++i)
    {
        REQUIRE(engine->runFrame());
    }
    CHECK(engine->grassQuality() == render::GrassQuality::Off);
    CHECK(engine->grassRing().tiles().empty());
    CHECK(engine->grassStats().nearTufts + engine->grassStats().farTufts == 0);
}

TEST_CASE("Engine grass GPU: no grass in Leonberg's rooms and houses (with the generated town)")
{
    if (!std::filesystem::exists(std::string(G7_ASSET_SOURCE_DIR) +
                                 "/worlds/leonberg/generated/leonberg_terrain.r16"))
    {
        MESSAGE("skipped: Leonberg's generated files are not here");
        return;
    }
    // In the lane in front of the Krumme Gans: houses, rooms and gardens around.
    auto engine = grassEngine("worlds/leonberg/leonberg.g7world", Vec3(-93.0f, 1.8f, 38.0f), "medium");
    f64 worst = 0.0;
    f64 total = 0.0;
    for (int i = 0; i < 60; ++i)
    {
        REQUIRE(engine->runFrame());
        worst = std::max(worst, engine->grassRing().lastBuildMilliseconds());
        total += engine->grassRing().lastBuildMilliseconds();
    }
    const usize tufts = checkTufts(*engine);
    MESSAGE(tufts, " tufts in town; tile building: worst frame ", worst, " ms, all ", total, " ms");
}
