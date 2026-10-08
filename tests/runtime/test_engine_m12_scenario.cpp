// M12 milestone (DoD, docs/03-roadmap.md, label "headless"): fire bolt, healing, sleep, transformation and
// summoning work, with the AI's reactions - in the test camp, with the starting set of runes (Z9), the herb
// witch and a bandit with a scroll (owner decision A). The hero is scripted (draw, rune keys, hold and
// release).

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
    config.appName = "m12";
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

void runSeconds(Engine& engine, f32 seconds)
{
    for (int i = 0; i < static_cast<int>(seconds * 60.0f); ++i)
    {
        REQUIRE(engine.runFrame());
    }
}

/// Holds the casting keys a moment and lets go; the clip plays out.
void cast(Engine& engine)
{
    run(engine, "hero_cast(true)");
    runSeconds(engine, 0.1f);
    run(engine, "hero_cast(false)");
    runSeconds(engine, 1.5f);
}

void place(Engine& engine, const char* npc, f32 x, f32 z, f32 yaw)
{
    REQUIRE(run(engine, std::format("insert_npc('{}', 'wp_camp_center')", npc)).isString());
    run(engine, std::format("set_routine('{0}', '') npc_clear('{0}') npc_teleport('{0}', {1}, 0, {2}, {3})",
                            npc, x, z, yaw));
}

std::string fightState(Engine& engine, std::string_view who)
{
    return std::string(run(engine, std::format("fight_state('{}')", who)).asString());
}
} // namespace

TEST_CASE(
    "M12 scenario: a mage hero against the herb witch and two bandits - sleep, fire bolt, a summoned wolf, "
    "healing, the wolf's shape; the AI answers")
{
    Engine engine(scenarioConfig());
    REQUIRE(engine.init().ok());
    run(engine, "Story.met_gate_guard = true");
    run(engine, "on('npc_cast', function(caster, spell) if caster ~= 'hero' then Story.npc_casts = "
                "(Story.npc_casts or '') .. caster .. ':' .. spell .. ' ' end end)");
    // The starting set (Z9) on the rune places 1-5; the third circle, a level for sleeping bandits (Z6).
    for (const char* rune : {"it_rune_firebolt", "it_rune_heal", "it_rune_sleep", "it_rune_summon_wolf",
                             "it_rune_transform_wolf"})
    {
        run(engine, std::format("give_item('{0}') equip('{0}')", rune));
    }
    run(engine, "set_talent('magic_circle', 3) add_xp(10000) set_stat('mana_max', 400) set_stat('mana', 400) "
                "set_stat('hp_max', 300) set_stat('hp', 300) teleport(41.2, 0, 18.8)");
    // West of him (he looks west): the herb witch 12 m away, a bandit beside her.
    place(engine, "npc_camp_hexer", 29.2f, 18.8f, 270.0f);
    place(engine, "npc_bandit", 30.2f, 21.8f, 270.0f);
    runSeconds(engine, 0.5f);

    // Sleep (Z6): the bandit, nearest and so locked, lies down.
    run(engine, "npc_teleport('npc_bandit', 33.2, 0, 18.8, 270)");
    runSeconds(engine, 0.3f);
    CHECK(run(engine, "draw_magic()").asString() == "it_rune_firebolt");
    run(engine, "hero_rune(3)");
    runSeconds(engine, 1.0f);
    REQUIRE(engine.heroCombatTarget() == std::optional<std::string>("npc_bandit"));
    cast(engine);
    CHECK(fightState(engine, "npc_bandit") == "down"); // asleep

    // Fire bolt at the witch: hit, she answers - fire bolts from afar (and heals when low).
    // The lock stays on a sleeper: away with the magic, the witch nearer, drawn again - now she is locked.
    run(engine, "draw_magic() npc_teleport('npc_camp_hexer', 35.2, 0, 18.3, 270)");
    runSeconds(engine, 1.0f);
    run(engine, "hero_rune(1)");
    runSeconds(engine, 1.0f);
    REQUIRE(engine.heroCombatTarget() == std::optional<std::string>("npc_camp_hexer"));
    const i64 witch = run(engine, "npc_stat('npc_camp_hexer', 'hp')").asInteger();
    cast(engine);
    CHECK(run(engine, "npc_stat('npc_camp_hexer', 'hp')").asInteger() < witch);
    runSeconds(engine, 3.0f);
    CHECK(std::string(run(engine, "tostring(Story.npc_casts)").asString()).find("npc_camp_hexer:spl_") !=
          std::string::npos);

    // A summoned wolf (Z8) joins in against whoever attacks the hero.
    run(engine, "hero_rune(4)");
    runSeconds(engine, 0.5f);
    cast(engine);
    const std::string wolf(run(engine, "tostring(hero_summon())").asString());
    CHECK(wolf.starts_with("mon_wolf"));
    runSeconds(engine, 8.0f);
    CHECK(run(engine, std::format("npc_state('{}').state", wolf)).asString() == "zs_attack");

    // Healing.
    run(engine, "set_stat('hp', 100) hero_rune(2)");
    runSeconds(engine, 0.5f);
    run(engine, "Story.taken = 0 on('npc_hit', function(attacker, target, damage) if target == 'hero' then "
                "Story.taken = Story.taken + damage end end)");
    const i64 before = run(engine, "stat('hp')").asInteger();
    cast(engine);
    // +50, minus what hit him meanwhile (the witch keeps casting).
    CHECK(run(engine, "stat('hp')").asInteger() == before + 50 - run(engine, "Story.taken").asInteger());

    // The wolf's shape (Z7): the guard sees a beast and attacks, the old man flees; "1" makes him human
    // again.
    run(engine, "hero_rune(5)");
    runSeconds(engine, 0.5f);
    cast(engine);
    REQUIRE(run(engine, "tostring(hero_shape())").asString() == "wolf");
    run(engine, "teleport(10, 0, 6)");                   // into the camp
    place(engine, "npc_old_man", 6.0f, 6.0f, 270.0f);    // looking east, at him
    place(engine, "npc_gate_guard", 14.0f, 6.0f, 90.0f); // looking west, at him
    runSeconds(engine, 3.0f);
    CHECK(run(engine, "npc_state('npc_old_man').state").asString() == "zs_flee");
    CHECK(run(engine, "npc_state('npc_gate_guard').state").asString() == "zs_attack");
    run(engine, "draw_magic()");
    runSeconds(engine, 1.0f); // the transition clips (figuren #255)
    CHECK(run(engine, "hero_shape()").isNil());
}
