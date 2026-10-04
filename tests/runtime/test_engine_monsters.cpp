// Animals (M9 part D, label "headless"): Npcs with a species - packs in their territory, sleeping by day or
// night, threatening and then attacking the player (wolf, boar), the neutral bird attacking only close up,
// hunting and fleeing among animals. Behaviour in game/scripts/ai/monsters.lua, values in data/creatures.lua.

#include <g7/runtime/Engine.hpp>

#include <doctest/doctest.h>

#include <cmath>
#include <format>
#include <ostream> // doctest needs it to print std::string operands
#include <string>

using namespace g7;

namespace
{
EngineConfig monsterConfig()
{
    EngineConfig config;
    config.appName = "monsters";
    config.headless = true;
    config.world = fs::fromUtf8("testworld/camp.g7world");
    config.start = "START_LAGER";
    config.startTime = "22:00";
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

void runSeconds(Engine& engine, f32 seconds)
{
    for (int i = 0; i < static_cast<int>(seconds * 60.0f); ++i)
    {
        REQUIRE(engine.runFrame());
    }
}

std::string stateOf(Engine& engine, std::string_view npc)
{
    return std::string(run(engine, std::format("npc_state('{}').state", npc)).asString());
}

Vec3 positionOf(Engine& engine, std::string_view npc)
{
    const auto c = [&](char axis)
    { return static_cast<f32>(run(engine, std::format("npc_state('{}').{}", npc, axis)).asNumber()); };
    return Vec3(c('x'), c('y'), c('z'));
}

f32 flat(const Vec3& a, const Vec3& b)
{
    return glm::length(Vec2(a.x - b.x, a.z - b.z));
}
} // namespace

TEST_CASE("Engine animals: a wolf pack roams its territory at night, the members near their leader")
{
    Engine engine(monsterConfig());
    REQUIRE(engine.init().ok());
    // Three wolves at their den (-35, 0, -20); the player in the camp, out of sight.
    CHECK(run(engine, "#insert_pack('mon_wolf', 3, 'wp_wolf_den')").asInteger() == 3);
    CHECK(run(engine, "npc_state('mon_wolf#3').routine").asString() == "rtn_mon_wolf");
    runSeconds(engine, 1.0f);
    CHECK(stateOf(engine, "mon_wolf") == "zs_mm_roam");
    const Vec3 den(-35.0f, 0.0f, -20.0f);
    for (int i = 0; i < 6; ++i)
    {
        runSeconds(engine, 10.0f);
        const Vec3 leader = positionOf(engine, "mon_wolf");
        CHECK(flat(leader, den) < 25.0f + 3.0f); // in the territory
        for (const char* member : {"mon_wolf#2", "mon_wolf#3"})
        {
            CHECK(flat(positionOf(engine, member), leader) <
                  15.0f); // with the pack (eating, trees: some lag)
        }
    }
    // By day they sleep: back to the den (the placeholder clips walk at 0.42 m/s), then lie down.
    run(engine, "time(9, 0)");
    runSeconds(engine, 2.0f);
    CHECK(stateOf(engine, "mon_wolf") == "zs_mm_sleep");
    for (int i = 0; i < 120 && run(engine, "npc_state('mon_wolf').ambient").asString() != "sleep"; ++i)
    {
        runSeconds(engine, 1.0f);
    }
    CHECK(run(engine, "npc_state('mon_wolf').ambient").asString() == "sleep");
    CHECK(flat(positionOf(engine, "mon_wolf"), den) < 2.0f);
}

TEST_CASE("Engine animals: wolves threaten a player who comes near, then attack; the bird only close up")
{
    Engine engine(monsterConfig());
    REQUIRE(engine.init().ok());
    run(engine, "on('npc_would_attack', function(npc, reason) Story.attack = npc .. ' ' .. reason end)");
    REQUIRE(run(engine, "#insert_pack('mon_wolf', 2, 'wp_wolf_den')").asInteger() == 2);
    runSeconds(engine, 1.0f);
    // 9 m from the leader (inside the 12 m), on a side he can see (trees stand around the den).
    for (int i = 0; i < 8; ++i)
    {
        const Vec3 wolf = positionOf(engine, "mon_wolf");
        const f32 angle = static_cast<f32>(i) * 0.785f;
        run(engine, std::format("teleport({}, 0, {})", wolf.x + 9.0f * std::cos(angle),
                                wolf.z + 9.0f * std::sin(angle)));
        runSeconds(engine, 1.0f);
        if (stateOf(engine, "mon_wolf") == "zs_mm_threaten")
        {
            break;
        }
    }
    CHECK(stateOf(engine, "mon_wolf") == "zs_mm_threaten");
    CHECK(stateOf(engine, "mon_wolf#2") == "zs_mm_threaten"); // the pack with it
    CHECK(run(engine, "Story.attack").isNil());
    runSeconds(engine, 4.0f); // threatening for 3 s
    CHECK(run(engine, "Story.attack").asString().starts_with("mon_wolf"));
    CHECK(stateOf(engine, "mon_wolf") == "zs_mm_attack");

    // The bird by day: neutral at 7 m, attacks at 3 m.
    run(engine, "time(12, 0)");
    run(engine, "Story.attack = nil");
    run(engine, "insert_animal('mon_laufvogel', 'wp_meadow_south')");
    run(engine, "set_routine('mon_laufvogel', '') npc_clear('mon_laufvogel')"); // it stands still
    run(engine, "teleport(-15, 0, 30)"); // the meadow is at (-15, 0, 45)
    runSeconds(engine, 1.0f);
    const Vec3 bird = positionOf(engine, "mon_laufvogel");
    run(engine, std::format("teleport({}, 0, {})", bird.x + 7.0f, bird.z));
    runSeconds(engine, 1.5f);
    CHECK(run(engine, "Story.attack").isNil());
    run(engine, std::format("teleport({}, 0, {})", bird.x + 3.0f, bird.z));
    runSeconds(engine, 1.5f);
    CHECK(run(engine, "Story.attack").asString() == "mon_laufvogel animal");
}

TEST_CASE("Engine animals: the wolf hunts the bird, the bird flees")
{
    Engine engine(monsterConfig());
    REQUIRE(engine.init().ok());
    run(engine, "time(6, 30)");         // both awake (wolves until 7, the bird from 6)
    run(engine, "teleport(30, 0, 50)"); // the player aside, out of their way
    // Both on the meadow (-15, 0, 45).
    run(engine, "insert_animal('mon_laufvogel', 'wp_meadow_south')");
    run(engine, "insert_animal('mon_wolf', 'wp_meadow_south')");
    // Within 15 m the wolf hunts, within 12 m the bird flees from it.
    bool hunted = false;
    bool fled = false;
    for (int i = 0; i < 90 && !(hunted && fled); ++i)
    {
        runSeconds(engine, 1.0f);
        hunted = hunted || stateOf(engine, "mon_wolf") == "zs_mm_hunt";
        fled = fled || stateOf(engine, "mon_laufvogel") == "zs_mm_flee";
    }
    CHECK(hunted);
    CHECK(fled);
    // The bird runs (the wolf is faster: 1.3 against 1.04 m/s with the placeholder clips).
    run(engine, "npc_start_state('mon_laufvogel', 'zs_mm_flee', 'mon_wolf')");
    const Vec3 start = positionOf(engine, "mon_laufvogel");
    runSeconds(engine, 4.0f);
    CHECK(flat(positionOf(engine, "mon_laufvogel"), start) > 2.0f);
}
