// Reflection probes in the engine (M17 windows F2): with window reflections medium the test scene (window
// glass, no waynet) gets probes on a 20 m grid 2.5 m above the ground, each with a box around it; the nearest
// are drawn at once, then one face a frame. Low places none, nor does a world without glass (the camp).

#include "GlFixture.hpp"

#include <g7/core/Config.hpp>
#include <g7/runtime/Engine.hpp>

#include <doctest/doctest.h>

#include <algorithm>
#include <format>
#include <memory>
#include <string_view>
#include <utility>

using namespace g7;

namespace
{
std::unique_ptr<Engine> probeEngine(std::string_view quality, bool scene = true)
{
    auto settings = Config::parse(std::format("[render]\nwindow_reflections = \"{}\"\n", quality));
    REQUIRE(settings.ok());
    EngineConfig config;
    config.appName = "probes";
    config.window.size = {320, 180};
    config.window.vsync = false;
    if (scene)
    {
        config.scene = fs::fromUtf8("testscene/scene.toml"); // with the glass wall (testscene/glass)
    }
    else
    {
        config.world = fs::fromUtf8("testworld/camp.g7world");
    }
    config.startTime = "12:00";
    config.shaderDirectory = fs::fromUtf8(G7_SHADER_DIR);
    config.fixedFrameSeconds = 1.0 / 60.0;
    config.player = false;
    config.settings = std::move(settings).value();
    config.view.camera = Vec3(0.0f, 1.6f, 11.0f);
    config.view.fly = true;
    test::keepVideoAlive();
    auto engine = std::make_unique<Engine>(config);
    REQUIRE(engine->init().ok());
    REQUIRE(engine->renderDevice() != nullptr);
    return engine;
}
} // namespace

TEST_CASE("Engine probes GPU: a grid over the test scene, the nearest at once, then one face a frame (F2)")
{
    auto engine = probeEngine("medium");
    // Placed once the world's collision stands (the first frame or soon after).
    for (int i = 0; i < 5 && engine->reflectionProbes().empty(); ++i)
    {
        REQUIRE(engine->runFrame());
    }
    const auto probes = engine->reflectionProbes();
    REQUIRE(!probes.empty());
    for (usize i = 0; i < probes.size(); ++i)
    {
        const render::ReflectionProbe& p = probes[i];
        CHECK(p.box.min.x < p.position.x);
        CHECK(p.box.max.x > p.position.x);
        CHECK(p.box.min.y < p.position.y);
        CHECK(p.box.max.z > p.position.z);
        for (usize j = i + 1; j < probes.size(); ++j)
        {
            const Vec3 d = probes[j].position - p.position;
            CHECK(glm::length(Vec2(d.x, d.z)) > 19.0f); // the grid's spacing
        }
    }
    const u32 first = engine->probeFacesLastFrame();
    CHECK(first > 1);
    CHECK(first <= 4 * render::kProbeFaces);
    for (int i = 0; i < 12; ++i)
    {
        REQUIRE(engine->runFrame());
        CHECK(engine->probeFacesLastFrame() <= 1);
    }
    CHECK(engine->probeSchedule().slotsReady() >= 1);
    CHECK(!engine->probeSchedule().shaderProbes().empty());

    // Low: none (no atlas).
    auto low = probeEngine("low");
    REQUIRE(low->runFrame());
    REQUIRE(low->runFrame());
    CHECK(low->reflectionProbes().empty());
    CHECK(low->probeFacesLastFrame() == 0);
    // A world without window glass needs none either.
    auto camp = probeEngine("medium", false);
    REQUIRE(camp->runFrame());
    REQUIRE(camp->runFrame());
    CHECK(camp->reflectionProbes().empty());
    CHECK(camp->probeFacesLastFrame() == 0);
}
