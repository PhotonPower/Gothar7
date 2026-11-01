// Lights per frame (render.md "Lichter je Bild", label "gpu"): in Leonberg - 220 light vobs and 151 window
// lights (welt's phase-3 measurement) - a frame takes at most max_lights (128), the torch always, window
// lights only near the eye; no "more than 256 point lights" any more. Needs the generated town (not
// versioned; skipped without).

#include "GlFixture.hpp"

#include <g7/runtime/Engine.hpp>

#include <doctest/doctest.h>

#include <algorithm>
#include <filesystem>
#include <string>

using namespace g7;

TEST_CASE("Engine lights GPU: Leonberg takes at most 128 lights a frame, the torch always (with the "
          "generated town)")
{
    if (!std::filesystem::exists(std::string(G7_ASSET_SOURCE_DIR) +
                                 "/worlds/leonberg/generated/leonberg_terrain.r16"))
    {
        MESSAGE("skipped: Leonberg's generated files are not here");
        return;
    }
    EngineConfig config;
    config.appName = "lights";
    config.window.size = {320, 180};
    config.window.vsync = false;
    config.world = fs::fromUtf8("worlds/leonberg/leonberg.g7world");
    config.start = "START_MARKTPLATZ";
    config.startTime = "22:00";
    config.shaderDirectory = fs::fromUtf8(G7_SHADER_DIR);
    config.fixedFrameSeconds = 1.0 / 60.0;
    test::keepVideoAlive();
    Engine engine(config);
    REQUIRE(engine.init().ok());
    REQUIRE(engine.renderDevice() != nullptr);
    CHECK(engine.worldLightCount() > render::LightList::kMaxPerFrame); // more than the shader holds
    for (int i = 0; i < 5; ++i)
    {
        REQUIRE(engine.runFrame());
    }
    CHECK(engine.frameLights().lights().size() <= 128);
    CHECK_FALSE(engine.frameLights().lights().empty());
    // The torch: lit in the hand, its flame's light comes first, whatever else is around.
    REQUIRE(engine.runConsoleLine("give_item('it_torch')").ok());
    REQUIRE(engine.runConsoleLine("hero_torch()").ok());
    for (int i = 0; i < 180; ++i)
    {
        REQUIRE(engine.runFrame());
    }
    REQUIRE(engine.runConsoleLine("hero_torch_lit()").value().asBool());
    const auto& lights = engine.frameLights().lights();
    REQUIRE_FALSE(lights.empty());
    CHECK(lights.size() <= 128);
    REQUIRE(engine.player() != nullptr);
    const f32 toHand = glm::length(lights.front().position - engine.player()->feet());
    CHECK(toHand < 2.5f); // the first light is at the hero: the torch
}
