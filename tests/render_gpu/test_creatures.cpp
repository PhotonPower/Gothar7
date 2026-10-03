// Animals in the engine (M6 part D3, label "gpu"): spawned on the ground, moved by root motion, turning,
// every action and the showcase without GL errors.

#include "GlFixture.hpp"

#include <g7/runtime/Engine.hpp>

#include <cmath>
#include <ostream> // doctest needs it to print std::string operands
#include <set>
#include <string>

using namespace g7;

namespace
{
EngineConfig creatureConfig()
{
    EngineConfig config;
    config.appName = "creatures";
    config.window.size = {160, 90};
    config.window.vsync = false;
    config.world = fs::fromUtf8("testworld/camp.g7world");
    config.start = "START_KLETTERPLATZ";
    config.shaderDirectory = fs::fromUtf8(G7_SHADER_DIR);
    config.fixedFrameSeconds = 1.0 / 60.0;
    g7::test::keepVideoAlive();
    return config;
}

void frames(Engine& engine, int n)
{
    for (int i = 0; i < n; ++i)
    {
        REQUIRE(engine.runFrame());
    }
}
} // namespace

TEST_CASE("Creatures GPU: on the ground, walking ahead, turning, every action")
{
    Engine engine(creatureConfig());
    auto result = engine.init();
    REQUIRE_MESSAGE(result.ok(), (result.ok() ? "" : result.error().message));
    CHECK_FALSE(engine.spawnCreature("dragon", Vec3(0.0f), 0.0f).ok());

    // Open ground of the camp (flat at 0), dropped from a little above.
    const auto wolf = engine.spawnCreature("wolf", Vec3(46.0f, 0.5f, -6.0f), 0.0f); // facing -Z
    const auto keiler = engine.spawnCreature("keiler", Vec3(42.0f, 0.5f, -6.0f), 0.0f);
    const auto vogel = engine.spawnCreature("laufvogel", Vec3(38.0f, 0.5f, -6.0f), 0.0f);
    REQUIRE(wolf.ok());
    REQUIRE(keiler.ok());
    REQUIRE(vogel.ok());
    CHECK(engine.creatureCount() == 3);
    frames(engine, 5);
    for (const u32 id : {wolf.value(), keiler.value(), vogel.value()})
    {
        CHECK(std::abs(engine.creaturePosition(id)->y) < 0.01f);
        CHECK(engine.creatureState(id) == "move");
    }

    // Walking: ahead (-Z), straight on, still on the ground.
    engine.setCreatureMove(wolf.value(), 1.1f); // run
    frames(engine, 120);
    const Vec3 ran = *engine.creaturePosition(wolf.value());
    CHECK(-6.0f - ran.z > 0.5f);
    CHECK(std::abs(ran.x - 46.0f) < 0.3f);
    CHECK(std::abs(ran.y) < 0.01f);
    engine.setCreatureMove(wolf.value(), 0.0f);
    frames(engine, 30);
    const Vec3 stopped = *engine.creaturePosition(wolf.value());
    frames(engine, 30);
    CHECK(glm::length(*engine.creaturePosition(wolf.value()) - stopped) < 0.02f);

    // Turning left on the spot: the yaw grows (positive left).
    const f32 yaw = engine.creatureYaw(keiler.value());
    engine.setCreatureMove(keiler.value(), 0.0f, -1.0f);
    frames(engine, 2);
    CHECK(engine.creatureState(keiler.value()) == "turn_l");
    engine.setCreatureMove(keiler.value(), 0.0f, 0.0f);
    frames(engine, 90);
    CHECK(glm::degrees(std::remainder(engine.creatureYaw(keiler.value()) - yaw, 2.0f * glm::pi<f32>())) >
          45.0f);
    CHECK(engine.creatureState(keiler.value()) == "move");

    // Actions.
    CHECK_FALSE(engine.creatureAction(vogel.value(), "fly"));
    for (const auto& [action, state] :
         {std::pair{"attack_1", "attack_1"}, std::pair{"threaten", "threaten"}, std::pair{"hit", "hit"},
          std::pair{"eat", "eat"}, std::pair{"sleep", "sleep"}, std::pair{"die", "die"}})
    {
        CAPTURE(action);
        REQUIRE(engine.creatureAction(vogel.value(), action));
        frames(engine, 3);
        CHECK(engine.creatureState(vogel.value()) == state);
        frames(engine, 60);
    }
    CHECK(engine.creatureState(vogel.value()) == "die"); // stays down
    CHECK(engine.creatureAction(vogel.value(), "revive"));
    CHECK(engine.creatureAction(vogel.value(), "stop"));
    frames(engine, 30);
    CHECK(engine.creatureState(vogel.value()) == "move");

    // The showcase goes through all states.
    engine.setCreatureShowcase(wolf.value(), true);
    std::set<std::string> seen;
    for (int i = 0; i < 28 * 60; ++i)
    {
        REQUIRE(engine.runFrame());
        seen.insert(std::string(engine.creatureState(wolf.value())));
    }
    for (const char* state :
         {"move", "turn_l", "turn_r", "attack_1", "attack_2", "threaten", "hit", "eat", "sleep", "die"})
    {
        CAPTURE(state);
        CHECK(seen.contains(state));
    }
    CHECK(engine.renderDevice()->debugErrorCount() == 0);
    engine.removeCreatures();
    CHECK(engine.creatureCount() == 0);
}

TEST_CASE("Scripts GPU: items and NPCs inserted from the console render, the console window draws")
{
    Engine engine(creatureConfig());
    auto result = engine.init();
    REQUIRE_MESSAGE(result.ok(), (result.ok() ? "" : result.error().message));
    REQUIRE(engine.runConsoleLine("insert('it_sword_old')").ok());
    REQUIRE(engine.runConsoleLine("insert('it_apple', 3)").ok());
    REQUIRE(engine.runConsoleLine("insert('npc_farmer_woman')").ok());
    engine.setConsoleOpen(true);
    frames(engine, 20);
    CHECK(engine.consoleOpen());
    CHECK(engine.creatureCount() == 1);
    CHECK(engine.renderDevice()->debugErrorCount() == 0);
}
