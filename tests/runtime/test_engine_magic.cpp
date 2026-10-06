// Magic (M12 part B, label "headless"): the spells, runes and scrolls of the content load; who may cast what
// - runes need the spell's magic circle, scrolls not, both the mana (Z1-Z3).

#include <g7/runtime/Engine.hpp>

#include <doctest/doctest.h>

#include <format>
#include <optional>
#include <ostream> // doctest needs it to print std::string operands
#include <string>

using namespace g7;

namespace
{
script::Value run(Engine& engine, std::string_view line)
{
    auto result = engine.runConsoleLine(line);
    const std::string what = std::string(line) + ": " + (result.ok() ? "" : result.error().message);
    REQUIRE_MESSAGE(result.ok(), what);
    return result.value();
}
} // namespace

TEST_CASE("Engine magic: the starting spells load; runes need the circle, scrolls not, both the mana")
{
    EngineConfig config;
    config.appName = "magic";
    config.headless = true;
    config.world = fs::fromUtf8("testworld/camp.g7world");
    config.start = "START_LAGER";
    Engine engine(std::move(config));
    REQUIRE(engine.init().ok());
    CHECK(engine.scripts()->errors().empty());
    for (const char* rune : {"it_rune_firebolt", "it_rune_heal", "it_rune_sleep", "it_rune_transform_wolf",
                             "it_rune_summon_wolf"})
    {
        const auto spell = engine.spellOfItem(rune);
        REQUIRE_MESSAGE(spell.has_value(), rune);
        CHECK(spell->mana > 0);
    }
    const auto sleep = engine.spellOfItem("it_rune_sleep");
    CHECK(sleep->circle == 2);
    CHECK(sleep->effect == "sleep");
    CHECK(sleep->duration == doctest::Approx(20.0f));
    CHECK(engine.spellOfItem("it_rune_summon_wolf")->summon == "mon_wolf");
    CHECK(engine.spellOfItem("it_rune_firebolt")->damage.at("fire") == 25);
    CHECK_FALSE(engine.spellOfItem("it_apple").has_value());

    run(engine, "give_item('it_rune_sleep') give_item('it_scroll_sleep') set_stat('mana_max', 30) "
                "set_stat('mana', 30)");
    // Z2: the rune wants the second circle.
    CHECK(run(engine, "cast_check('it_rune_sleep')").asString() ==
          "Dafür fehlt dir der zweite Kreis der Magie.");
    CHECK(run(engine, "cast_check('it_scroll_sleep')").isNil()); // Z3: a scroll without a circle
    run(engine, "set_talent('magic_circle', 2)");
    CHECK(run(engine, "cast_check('it_rune_sleep')").isNil());
    // Z1/Z3: the mana, from a rune and from a scroll alike.
    run(engine, "set_stat('mana', 10)");
    CHECK(run(engine, "cast_check('it_rune_sleep')").asString() == "Dafür reicht dein Mana nicht.");
    CHECK(run(engine, "cast_check('it_scroll_sleep')").asString() == "Dafür reicht dein Mana nicht.");
    CHECK(run(engine, "cast_check('it_rune_heal')").asString() == "Das hast du nicht.");
}

namespace
{
EngineConfig castConfig()
{
    EngineConfig config;
    config.appName = "magic";
    config.headless = true;
    config.world = fs::fromUtf8("testworld/camp.g7world");
    config.start = "START_LAGER";
    config.startTime = "12:00";
    config.fixedFrameSeconds = 1.0 / 60.0;
    return config;
}

void runSeconds(Engine& engine, f32 seconds)
{
    for (int i = 0; i < static_cast<int>(seconds * 60.0f); ++i)
    {
        REQUIRE(engine.runFrame());
    }
}

i64 stat(Engine& engine, std::string_view who, std::string_view name)
{
    return run(engine, who == "hero" ? std::format("stat('{}')", name)
                                     : std::format("npc_stat('{}', '{}')", who, name))
        .asInteger();
}

/// Holds the casting keys for `hold` seconds and lets go; then the clip plays out.
void cast(Engine& engine, f32 hold = 0.1f)
{
    run(engine, "hero_cast(true)");
    runSeconds(engine, hold);
    run(engine, "hero_cast(false)");
    runSeconds(engine, 1.5f);
}

/// The hero at the camp looking west, an old man 12 m ahead, routines off.
void oldManAhead(Engine& engine)
{
    run(engine, "Story.met_gate_guard = true");
    run(engine, "teleport(41.2, 0, 18.8)");
    REQUIRE(run(engine, "insert_npc('npc_old_man', 'wp_camp_center')").isString());
    run(engine,
        "set_routine('npc_old_man', '') npc_clear('npc_old_man') npc_teleport('npc_old_man', 29.2, 0, "
        "18.8, 270)");
    runSeconds(engine, 0.3f);
}
} // namespace

TEST_CASE(
    "Engine casting: a rune drawn with 1, the fire bolt flies at the locked target and costs mana (Z4, Z5)")
{
    Engine engine(castConfig());
    REQUIRE(engine.init().ok());
    run(engine, "on('npc_cast', function(caster, spell) Story.cast = caster .. ' ' .. spell end)");
    run(engine,
        "give_item('it_rune_firebolt') equip('it_rune_firebolt') give_item('it_rune_heal') "
        "equip('it_rune_heal') set_talent('magic_circle', 1) set_stat('mana_max', 30) set_stat('mana', 30)");
    oldManAhead(engine);
    CHECK(run(engine, "draw_magic()").asString() == "it_rune_firebolt"); // the first rune place
    runSeconds(engine, 1.0f);
    CHECK(run(engine, "player_weapon()").asString() == "magic");
    REQUIRE(engine.heroCombatTarget() == std::optional<std::string>("npc_old_man")); // locked, as with a bow
    const i64 before = stat(engine, "npc_old_man", "hp");
    cast(engine);
    CHECK(run(engine, "Story.cast").asString() == "hero spl_firebolt");
    CHECK(stat(engine, "hero", "mana") == 20);
    CHECK(stat(engine, "npc_old_man", "hp") == before - 25); // fire 25, no protection; no arrow stays
    CHECK(run(engine, "npc_item_count('npc_old_man', 'it_rune_firebolt')").asInteger() == 0);
    CHECK(run(engine, "hero_casting()").isNil());

    // Too little mana: the attempt fails with a notice, nothing is spent (Z5).
    run(engine, "set_stat('mana', 5) Story.cast = nil");
    cast(engine);
    CHECK(run(engine, "Story.cast").isNil());
    CHECK(stat(engine, "hero", "mana") == 5);

    // Key 5 (place 2): the healing rune in the hand at once; it heals the hero.
    run(engine, "set_stat('mana', 30) set_stat('hp', 10)");
    run(engine, "hero_rune(2)");
    cast(engine);
    CHECK(run(engine, "Story.cast").asString() == "hero spl_heal");
    CHECK(stat(engine, "hero", "hp") == std::min<i64>(60, stat(engine, "hero", "hp_max")));
    // 1 again: away.
    CHECK(run(engine, "draw_magic()").asString() == "none");
}

TEST_CASE(
    "Engine casting: sleep up to the caster's level, damage wakes, the time runs out; a scroll is used up "
    "(Z3, Z6)")
{
    Engine engine(castConfig());
    REQUIRE(engine.init().ok());
    run(engine, "on('npc_asleep', function(npc, caster) Story.asleep = npc end)");
    run(engine, "on('npc_woke', function(npc) Story.woke = npc end)");
    run(engine, "give_item('it_scroll_sleep', 3) equip('it_scroll_sleep') give_item('it_rune_firebolt') "
                "equip('it_rune_firebolt') set_talent('magic_circle', 1) set_stat('mana_max', 100) "
                "set_stat('mana', 100)");
    oldManAhead(engine);
    CHECK(run(engine, "draw_magic()").asString() == "it_scroll_sleep");
    runSeconds(engine, 1.0f);
    // Level 0 against the old man's 1: no effect - the scroll and the mana are gone all the same.
    cast(engine);
    CHECK(run(engine, "Story.asleep").isNil());
    CHECK(run(engine, "item_count('it_scroll_sleep')").asInteger() == 2);
    CHECK(stat(engine, "hero", "mana") == 85);

    run(engine, "add_xp(500)"); // level 1
    cast(engine);
    CHECK(run(engine, "Story.asleep").asString() == "npc_old_man");
    CHECK(run(engine, "fight_state('npc_old_man')").asString() == "down");
    runSeconds(engine, 5.0f);
    CHECK(run(engine, "fight_state('npc_old_man')").asString() == "down"); // still asleep
    // A fire bolt wakes him (Z6).
    run(engine, "hero_rune(2)");
    runSeconds(engine, 0.2f);
    cast(engine);
    CHECK(run(engine, "Story.woke").asString() == "npc_old_man");
    CHECK(run(engine, "fight_state('npc_old_man')").asString() != "down");

    // The last scroll: asleep again; after 20 s he wakes by himself. The hands are empty then.
    run(engine, "hero_rune(1) Story.woke = nil npc_teleport('npc_old_man', 29.2, 0, 18.8, 270)");
    runSeconds(engine, 0.5f);
    cast(engine);
    CHECK(run(engine, "item_count('it_scroll_sleep')").asInteger() == 0);
    CHECK(run(engine, "player_weapon()").asString() == "none");
    CHECK(run(engine, "fight_state('npc_old_man')").asString() == "down");
    runSeconds(engine, 20.0f);
    CHECK(run(engine, "Story.woke").asString() == "npc_old_man");
}

TEST_CASE(
    "Engine casting: held, a spell charges a stage per second as far as the mana goes; released it acts "
    "(Z5)")
{
    Engine engine(castConfig());
    REQUIRE(engine.init().ok());
    // A test spell with three stages of 5 mana each (the starting set has none).
    run(engine, "Spell 'spl_test_charge' { name = 'Test', circle = 1, mana = 10, kind = 'self', heal = 10, "
                "invest = { stages = 3, mana = 5 } } "
                "Item 'it_rune_test_charge' { name = 'Test', category = 'rune', spell = 'spl_test_charge', "
                "value = 1 }");
    run(engine, "give_item('it_rune_test_charge') equip('it_rune_test_charge') set_talent('magic_circle', 1) "
                "set_stat('mana_max', 20) set_stat('mana', 20) set_stat('hp', 1)");
    CHECK(run(engine, "draw_magic()").asString() == "it_rune_test_charge");
    runSeconds(engine, 1.0f);
    run(engine, "hero_cast(true)");
    runSeconds(engine, 1.2f);
    CHECK(run(engine, "hero_casting()").asString() == "spl_test_charge 1");
    runSeconds(engine, 3.0f); // 4 s held: three stages wanted, the mana is enough for two (10 + 2 * 5)
    CHECK(run(engine, "hero_casting()").asString() == "spl_test_charge 2");
    CHECK(stat(engine, "hero", "mana") == 20); // paid on release
    run(engine, "hero_cast(false)");
    runSeconds(engine, 1.5f);
    CHECK(stat(engine, "hero", "mana") == 0);
    CHECK(stat(engine, "hero", "hp") == 31); // heal 10, three times as strong at stage 2
}
