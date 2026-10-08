// Footsteps (M13 part E, label "headless"): the material under the foot - water in the pond, a mob (the
// chest: wood), a path pattern (the stump stool: wood), components.surface.footstep on a vob (overrides), the
// terrain's splat layers - and the hero's steps sounding with it while he walks.

#include <g7/gameplay/Movement.hpp>
#include <g7/runtime/Engine.hpp>
#include <g7/world/Components.hpp>

#include <doctest/doctest.h>

#include <format>
#include <ostream> // doctest needs it to print std::string operands
#include <string>

using namespace g7;

namespace
{
EngineConfig footstepConfig()
{
    EngineConfig config;
    config.appName = "footsteps";
    config.headless = true;
    config.world = fs::fromUtf8("testworld/camp.g7world");
    config.start = "START_LAGER";
    config.startTime = "12:00";
    config.fixedFrameSeconds = 1.0 / 60.0;
    return config;
}

script::Value run(Engine& engine, std::string_view line)
{
    auto result = engine.runConsoleLine(line);
    const std::string what = std::string(line) + ": " + (result.ok() ? "" : result.error().message);
    REQUIRE_MESSAGE(result.ok(), what);
    return result.value();
}

std::string material(Engine& engine, f32 x, f32 y, f32 z)
{
    return std::string(run(engine, std::format("footstep_material({}, {}, {})", x, y, z)).asString());
}
} // namespace

TEST_CASE("Engine footsteps: the material under the foot - water, mobs, patterns, the vob's surface, terrain")
{
    Engine engine(footstepConfig());
    REQUIRE(engine.init().ok());
    REQUIRE(engine.runFrame());
    // The pond (top at y = -4): half a metre deep.
    CHECK(material(engine, -160.0f, -4.5f, -137.0f) == "water");
    // On the chest (a mob without a pattern: wood) and on the stump stool (pattern */nature/stump*).
    CHECK(material(engine, 29.0f, 0.65f, -4.0f) == "wood");
    CHECK(material(engine, 3.0f, 0.5f, 3.0f) == "wood");
    // The vob's own surface wins (components.surface.footstep, world.md).
    const entt::entity chest = engine.scene().findById(world::VobId{198});
    const bool found = chest != entt::null; // compared outside REQUIRE (doctest and entt both offer ==)
    REQUIRE(found);
    engine.scene().set<world::SurfaceRef>(chest, {"stone"});
    CHECK(material(engine, 29.0f, 0.65f, -4.0f) == "stone");
    // The terrain: its splat layers mapped (grass, earth -> dirt, rock -> stone, path -> gravel). Sampled
    // over the whole map from high above (the ray finds no model there).
    run(engine, "Story.seen = {} for x = -250, 250, 10 do for z = -250, 250, 10 do "
                "Story.seen[footstep_material(x, 200, z)] = true end end");
    for (const char* m : {"grass", "dirt", "gravel"})
    {
        CHECK_MESSAGE(run(engine, std::format("Story.seen['{}'] == true", m)).asBool(), m);
    }
    CHECK(run(engine, "Story.seen['water'] == nil and Story.seen['wood'] == nil").asBool());
}

TEST_CASE("Engine footsteps: the hero's steps sound with the material he walks on")
{
    Engine engine(footstepConfig());
    REQUIRE(engine.init().ok());
    run(engine, "teleport(40, 0, 30)"); // the open meadow south of the camp
    gameplay::MoveInput forward;
    forward.forward = 1.0f;
    engine.setPlayerInputOverride(forward);
    for (int i = 0; i < 120; ++i)
    {
        REQUIRE(engine.runFrame());
    }
    engine.setPlayerInputOverride(std::nullopt);
    const std::string last = std::string(run(engine, "last_footstep()").asString());
    CHECK_FALSE(last.empty());
    u32 steps = 0;
    for (const char* m : {"stone", "wood", "grass", "dirt", "gravel", "water"})
    {
        steps += engine.soundsPlayed(std::string("footstep_") + m);
    }
    CHECK(steps >= 2); // two seconds of running: several steps
    CHECK(engine.soundsPlayed("footstep_" + last) >= 1);
}
