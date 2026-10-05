// M11 milestone (DoD, docs/03-roadmap.md, label "headless"): fights against a man, a wolf pack and a strong
// enemy work, and the talent levels are felt - the same hero with talent 2 beats a bandit clearly faster than
// with talent 0. The hero is played by a simple bot: weapon drawn (the lock turns him to the enemy), strikes
// whenever he can, parries now and then while the enemy swings. Placeholder enemies in
// game/scripts/npcs/camp/bandits.lua.

#include <g7/runtime/Engine.hpp>

#include <doctest/doctest.h>

#include <format>
#include <ostream> // doctest needs it to print std::string operands
#include <string>

using namespace g7;

namespace
{
EngineConfig scenarioConfig()
{
    EngineConfig config;
    config.appName = "m11";
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

std::string state(Engine& engine, std::string_view who)
{
    return std::string(run(engine, std::format("fight_state('{}')", who)).asString());
}

/// The hero's bot against `enemies` for at most `limit` seconds: seconds until none of them stands (or
/// `limit`).
f32 fight(Engine& engine, std::initializer_list<const char*> enemies, f32 limit)
{
    f32 t = 0.0f;
    u32 tick = 0;
    for (; t < limit; t += 0.1f, ++tick)
    {
        bool standing = false;
        for (const char* e : enemies)
        {
            const std::string s = state(engine, e);
            standing = standing || (s != "down" && s != "dead");
        }
        if (!standing)
        {
            return t;
        }
        if (state(engine, "hero") == "ready")
        {
            // Parry now and then while the locked enemy swings, else strike when in reach.
            const auto target = engine.heroCombatTarget();
            if (target && state(engine, *target) == "attack" && tick % 3 == 0)
            {
                run(engine, "hero_parry()");
            }
            else if (target && run(engine, std::format("npc_distance('{}', 'hero')", *target)).asNumber() <
                                   run(engine, "npc_reach('hero')").asNumber())
            {
                run(engine, "hero_attack()");
            }
        }
        else if (state(engine, "hero") == "attack")
        {
            run(engine, "hero_attack()"); // within the combo window: the next hit
        }
        for (int i = 0; i < 6; ++i)
        {
            REQUIRE(engine.runFrame());
        }
    }
    return limit;
}

/// A fresh camp, the hero with the old sword at `talent`, in the open south of the camp.
void prepare(Engine& engine, int talent)
{
    REQUIRE(engine.init().ok());
    run(engine, "Story.met_gate_guard = true");
    // An early hero: strength 15, the old sword (edge 18).
    run(engine, std::format("set_stat('str', 15) set_stat('hp_max', 200) set_stat('hp', 200) "
                            "give_item('it_sword_old') equip('it_sword_old') set_talent('melee_1h', {})",
                            talent));
    run(engine, "teleport(40, 0, 30)");
}

/// The enemy 3 m ahead of the hero (he looks along -X), attacking him.
void enemyAhead(Engine& engine, std::string_view npc)
{
    REQUIRE(run(engine, std::format("insert_npc('{}', 'wp_camp_center')", npc)).isString());
    run(engine, std::format("set_routine('{}', '') npc_teleport('{}', 37, 0, 30, 270)", npc, npc));
    run(engine, "draw_weapon()");
    for (int i = 0; i < 30; ++i)
    {
        REQUIRE(engine.runFrame());
    }
    run(engine, std::format("fight('{}', 'hero')", npc));
}

f32 duel(int talent)
{
    Engine engine(scenarioConfig());
    prepare(engine, talent);
    enemyAhead(engine, "npc_bandit");
    const f32 seconds = fight(engine, {"npc_bandit"}, 90.0f);
    CHECK(state(engine, "npc_bandit") == "down"); // K7: a man beaten by a man falls unconscious
    return seconds;
}
} // namespace

TEST_CASE("M11 scenario: a bandit, talent felt - trained, the hero wins clearly faster")
{
    const f32 untrained = duel(0);
    const f32 trained = duel(2);
    MESSAGE("bandit down after ", untrained, " s (talent 0) and ", trained, " s (talent 2)");
    CHECK(untrained < 90.0f);
    CHECK(trained < untrained * 0.8f);
}

TEST_CASE("M11 scenario: a wolf pack of three - two attack at a time, none of them stays")
{
    Engine engine(scenarioConfig());
    prepare(engine, 1);
    run(engine, "teleport(-41, 0, -26)"); // at the wolf den
    REQUIRE(run(engine, "#insert_pack('mon_wolf', 3, 'wp_wolf_den')").asInteger() == 3);
    run(engine, "draw_weapon()");
    for (const char* wolf : {"mon_wolf", "mon_wolf#2", "mon_wolf#3"})
    {
        run(engine, std::format("set_routine('{}', '') fight('{}', 'hero')", wolf, wolf));
    }
    const f32 seconds = fight(engine, {"mon_wolf", "mon_wolf#2", "mon_wolf#3"}, 120.0f);
    MESSAGE("pack done after ", seconds, " s; the hero has ", run(engine, "stat('hp')").asInteger(),
            " of 200");
    for (const char* wolf : {"mon_wolf", "mon_wolf#2", "mon_wolf#3"})
    {
        // A wounded wolf may have run off (below a fifth of its life): dead or no longer attacking.
        const bool gone =
            state(engine, wolf) == "dead" ||
            run(engine, std::format("npc_state('{}').state", wolf)).asString() != "zs_mm_attack";
        CHECK_MESSAGE(gone, wolf);
    }
    int dead = 0;
    for (const char* wolf : {"mon_wolf", "mon_wolf#2", "mon_wolf#3"})
    {
        dead += state(engine, wolf) == "dead" ? 1 : 0;
    }
    CHECK(dead >= 2); // one may have fled wounded
}

TEST_CASE("M11 scenario: the strong enemy beats an untrained hero, a trained and armoured one beats him")
{
    {
        Engine engine(scenarioConfig());
        prepare(engine, 0);
        run(engine, "set_stat('hp_max', 120) set_stat('hp', 120)");
        enemyAhead(engine, "npc_bandit_leader");
        bool heroDown = false;
        for (int i = 0; i < 600 && !heroDown; ++i)
        {
            fight(engine, {"npc_bandit_leader"}, 0.1f);
            heroDown = state(engine, "hero") == "down";
        }
        CHECK(heroDown); // K8: down, not dead
        CHECK(state(engine, "npc_bandit_leader") != "down");
    }
    Engine engine(scenarioConfig());
    prepare(engine, 2);
    run(engine, "set_stat('str', 60) give_item('it_armor_leather') equip('it_armor_leather')");
    enemyAhead(engine, "npc_bandit_leader");
    const f32 seconds = fight(engine, {"npc_bandit_leader"}, 120.0f);
    MESSAGE("the leader is down after ", seconds, " s");
    CHECK(state(engine, "npc_bandit_leader") == "down");
}
