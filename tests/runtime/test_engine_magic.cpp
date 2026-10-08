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
    CHECK(engine.soundsPlayed("spell_cast") == 1);           // its sounds (M13)
    CHECK(engine.soundsPlayed("fire_impact") == 1);
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

TEST_CASE(
    "Engine casting: a summoned wolf follows the hero, fights his attacker, vanishes after its time; one at "
    "a time (Z8)")
{
    Engine engine(castConfig());
    REQUIRE(engine.init().ok());
    run(engine, "on('npc_vanished', function(npc) Story.vanished = npc end)");
    run(engine, "give_item('it_rune_summon_wolf') equip('it_rune_summon_wolf') set_talent('magic_circle', 3) "
                "set_stat('mana_max', 100) set_stat('mana', 100) teleport(41.2, 0, 18.8)");
    runSeconds(engine, 0.3f);
    CHECK(run(engine, "draw_magic()").asString() == "it_rune_summon_wolf");
    runSeconds(engine, 1.0f);
    cast(engine);
    const std::string wolf(run(engine, "hero_summon()").asString());
    REQUIRE_FALSE(wolf.empty());
    CHECK(stat(engine, "hero", "mana") == 75);
    CHECK(run(engine, std::format("npc_state('{}').state", wolf)).asString() == "zs_summoned");
    CHECK_FALSE(engine.heroCombatTarget().has_value()); // the hero does not lock his own wolf

    // It follows the hero.
    run(engine, "draw_magic() teleport(33.2, 0, 18.8)");
    runSeconds(engine, 8.0f);
    CHECK(run(engine, std::format("npc_distance('{}', 'hero')", wolf)).asNumber() < 5.0);

    // An old man attacks the hero: the wolf goes for him.
    run(engine, "Story.met_gate_guard = true");
    REQUIRE(run(engine, "insert_npc('npc_old_man', 'wp_camp_center')").isString());
    run(engine,
        "set_routine('npc_old_man', '') npc_clear('npc_old_man') npc_teleport('npc_old_man', 32.0, 0, "
        "18.8, 270) fight('npc_old_man', 'hero')"); // beside the hero, facing him
    const i64 before = stat(engine, "npc_old_man", "hp");
    runSeconds(engine, 10.0f);
    CHECK(stat(engine, "npc_old_man", "hp") < before);

    // A second call: the first wolf goes, a new one comes.
    run(engine, "set_stat('mana', 100)");
    CHECK(run(engine, "draw_magic()").asString() == "it_rune_summon_wolf");
    runSeconds(engine, 1.0f);
    cast(engine);
    CHECK(run(engine, "Story.vanished").asString() == wolf);
    CHECK(run(engine, std::format("fight_state('{}')", wolf)).asString() == "dead");
    const std::string second(run(engine, "hero_summon()").asString());
    CHECK_FALSE(second.empty());
    CHECK(second != wolf);
    // After 60 s it goes too.
    runSeconds(engine, 61.0f);
    CHECK(run(engine, "Story.vanished").asString() == second);
    CHECK(run(engine, "hero_summon()").isNil());
}

TEST_CASE(
    "Engine casting: in a wolf's shape the hero runs and bites as the wolf, with its life; back with 1 or "
    "when that life is gone (Z7)")
{
    Engine engine(castConfig());
    REQUIRE(engine.init().ok());
    run(engine, "on('hero_transformed', function(species) Story.shape = species end)");
    run(engine,
        "give_item('it_rune_transform_wolf') equip('it_rune_transform_wolf') set_talent('magic_circle', 2) "
        "set_stat('mana_max', 100) set_stat('mana', 100) set_stat('hp', 40)");
    oldManAhead(engine);
    const f32 humanRun = engine.movementSettings().runSpeed;
    const f32 humanCamera = engine.playerCameraDistance();
    CHECK(run(engine, "draw_magic()").asString() == "it_rune_transform_wolf");
    runSeconds(engine, 1.0f);
    cast(engine);
    REQUIRE(run(engine, "hero_shape()").isString());
    CHECK(run(engine, "hero_shape()").asString() == "wolf");
    CHECK(run(engine, "Story.shape").asString() == "wolf");
    CHECK(stat(engine, "hero", "mana") == 80);
    CHECK(engine.movementSettings().runSpeed == doctest::Approx(6.0f)); // the wolf's run (its clips)
    CHECK(engine.playerFigurePath().find("wolf") != std::string_view::npos);
    runSeconds(engine, 1.0f);
    CHECK(engine.playerCameraDistance() < 0.7f * humanCamera); // the camera at the wolf's height, nearer
    // No weapons, no magic, no bag in its paws.
    CHECK(run(engine, "player_weapon()").asString() == "animal");
    CHECK(run(engine, "draw_weapon()").asString() == "animal");
    engine.setInventoryOpen(true);
    CHECK_FALSE(engine.inventoryOpen());

    // It bites with the wolf's values: strength 20 + edge 0, no protection.
    run(engine, "npc_teleport('npc_old_man', 40.0, 0, 18.8, 270)"); // 1.2 m ahead
    runSeconds(engine, 0.5f);
    const i64 before = stat(engine, "npc_old_man", "hp");
    REQUIRE(run(engine, "hero_attack()").asBool());
    runSeconds(engine, 1.5f);
    CHECK(stat(engine, "npc_old_man", "hp") == before - 20);

    // "1": human again, with his own life (the old man, bitten, fights back from now on).
    run(engine, "draw_magic()");
    REQUIRE(engine.runFrame());
    CHECK(run(engine, "hero_shape()").isNil());
    CHECK(run(engine, "Story.shape").asString().empty());
    CHECK(engine.movementSettings().runSpeed == doctest::Approx(humanRun));
    CHECK(run(engine, "player_weapon()").asString() == "none");
    CHECK(stat(engine, "hero", "hp") == 40);

    // Again a wolf; the old man beats it until its life is gone: human again, his own life untouched.
    run(engine, "set_stat('mana', 100)");
    CHECK(run(engine, "draw_magic()").asString() == "it_rune_transform_wolf");
    runSeconds(engine, 1.0f);
    cast(engine);
    REQUIRE(run(engine, "hero_shape()").isString());
    const i64 human = stat(engine, "hero", "hp");
    run(engine, "fight('npc_old_man', 'hero')");
    for (int i = 0; i < 60 * 60 && run(engine, "hero_shape()").isString(); ++i)
    {
        REQUIRE(engine.runFrame());
    }
    CHECK(run(engine, "hero_shape()").isNil());
    CHECK(stat(engine, "hero", "hp") == human); // the wolf's life was beaten, not his
}

TEST_CASE("Engine reactions: people see a beast in the wolf-hero - guards attack, the old man flees; a wolf "
          "leaves him alone, the runner bird flees (M12 part D)")
{
    Engine engine(castConfig());
    REQUIRE(engine.init().ok());
    run(engine,
        "Story.met_gate_guard = true give_item('it_rune_transform_wolf') equip('it_rune_transform_wolf') "
        "set_talent('magic_circle', 2) set_stat('mana_max', 100) set_stat('mana', 100) "
        "teleport(41.2, 0, 18.8)");
    runSeconds(engine, 0.3f);
    CHECK(run(engine, "draw_magic()").asString() == "it_rune_transform_wolf");
    runSeconds(engine, 1.0f);
    cast(engine);
    REQUIRE(run(engine, "hero_shape()").isString());
    // Around him, all looking at him (he looks west, along -X).
    const auto place = [&](const char* npc, f32 x, f32 z, f32 yaw)
    {
        const bool animal = std::string_view(npc).starts_with("mon_");
        REQUIRE(run(engine,
                    std::format("{}('{}', 'wp_camp_center')", animal ? "insert_animal" : "insert_npc", npc))
                    .isString());
        run(engine,
            std::format("set_routine('{0}', '') npc_clear('{0}') npc_teleport('{0}', {1}, 0, {2}, {3})", npc,
                        x, z, yaw));
    };
    place("npc_old_man", 36.2f, 18.8f, 270.0f);
    place("npc_gate_guard", 41.2f, 13.8f, 180.0f);
    place("mon_wolf", 41.2f, 23.8f, 0.0f);
    place("mon_laufvogel", 46.2f, 18.8f, 90.0f);
    runSeconds(engine, 3.0f);
    CHECK(run(engine, "npc_state('npc_old_man').state").asString() == "zs_flee");
    CHECK(run(engine, "npc_state('npc_gate_guard').state").asString() == "zs_attack");
    CHECK(run(engine, "Fights['npc_gate_guard']").asString() == "hero");
    CHECK(run(engine, "npc_state('mon_wolf').state").asString() != "zs_mm_attack"); // his own kind
    CHECK(run(engine, "npc_state('mon_wolf').state").asString() != "zs_mm_threaten");
    CHECK(run(engine, "npc_state('mon_laufvogel').state").asString() == "zs_mm_flee");
}

TEST_CASE("Engine casting: fear makes the target flee for its time (Z6)")
{
    Engine engine(castConfig());
    REQUIRE(engine.init().ok());
    run(engine,
        "Spell 'spl_test_fear' { name = 'Test', circle = 1, mana = 5, kind = 'target', effect = 'fear', "
        "duration = 5 } "
        "Item 'it_rune_test_fear' { name = 'Test', category = 'rune', spell = 'spl_test_fear', value = 1 }");
    run(engine, "on('npc_feared', function(npc, caster, seconds) Story.feared = npc .. ' ' .. seconds end)");
    run(engine, "give_item('it_rune_test_fear') equip('it_rune_test_fear') set_talent('magic_circle', 1) "
                "set_stat('mana_max', 20) set_stat('mana', 20)");
    oldManAhead(engine);
    CHECK(run(engine, "draw_magic()").asString() == "it_rune_test_fear");
    runSeconds(engine, 1.0f);
    cast(engine);
    CHECK(run(engine, "Story.feared").asString().starts_with("npc_old_man 5"));
    CHECK(run(engine, "npc_state('npc_old_man').state").asString() == "zs_fear");
    runSeconds(engine, 3.0f);
    CHECK(run(engine, "npc_state('npc_old_man').state").asString() == "zs_fear"); // still fleeing
    runSeconds(engine, 3.0f);
    CHECK(run(engine, "npc_state('npc_old_man').state").asString() != "zs_fear");
}

TEST_CASE(
    "Engine NPC magic: a caster fights with fire bolts from afar, heals itself when low; its mana runs out "
    "(M12 part D)")
{
    Engine engine(castConfig());
    REQUIRE(engine.init().ok());
    // A test caster (no fixed content in the camp yet: the owner decides who casts).
    run(engine, "Npc 'npc_test_caster' { name = 'Zauberer', guild = 'bandit', level = 10, "
                "attributes = { hp = 100, mana = 45 }, talents = { magic_circle = 1 }, "
                "spells = { 'spl_heal', 'spl_firebolt' } }");
    run(engine,
        "on('npc_cast', function(caster, spell) Story.casts = (Story.casts or '') .. spell .. ' ' end)");
    run(engine,
        "Story.met_gate_guard = true teleport(41.2, 0, 18.8) set_stat('hp_max', 400) set_stat('hp', 400)");
    REQUIRE(run(engine, "insert_npc('npc_test_caster', 'wp_camp_center')").isString());
    run(engine, "set_routine('npc_test_caster', '') npc_clear('npc_test_caster') "
                "npc_teleport('npc_test_caster', 31.2, 0, 18.8, 270)");
    runSeconds(engine, 0.3f);
    // Directly: a fire bolt at the hero, 10 m away.
    REQUIRE(run(engine, "npc_cast_spell('npc_test_caster', 'spl_firebolt', 'hero')").asBool());
    CHECK(run(engine, "npc_casting('npc_test_caster')").asBool());
    CHECK_FALSE(run(engine, "npc_cast_spell('npc_test_caster', 'spl_firebolt', 'hero')").asBool()); // busy
    runSeconds(engine, 2.0f);
    CHECK(run(engine, "stat('hp')").asInteger() == 400 - 25);
    CHECK(run(engine, "npc_stat('npc_test_caster', 'mana')").asInteger() == 35);
    CHECK_FALSE(run(engine, "npc_casting('npc_test_caster')").asBool());

    // In a fight: low on life it heals itself first, then bolts at the hero while its mana lasts; then it
    // walks up to strike.
    run(engine, "npc_set_stat('npc_test_caster', 'hp', 20) Story.casts = ''");
    run(engine, "fight('npc_test_caster', 'hero')");
    runSeconds(engine, 12.0f);
    const std::string casts(run(engine, "Story.casts").asString());
    INFO(casts);
    CHECK(casts.starts_with("spl_heal"));
    CHECK(casts.find("spl_firebolt") != std::string::npos);
    CHECK(run(engine, "npc_stat('npc_test_caster', 'mana')").asInteger() < 10); // spent
    CHECK(run(engine, "npc_stat('npc_test_caster', 'hp')").asInteger() >= 70);
}

TEST_CASE(
    "Engine burning: only a spell with burn sets on fire - three seconds, five a second; the fire bolt does "
    "not (owner decision B)")
{
    Engine engine(castConfig());
    REQUIRE(engine.init().ok());
    run(engine,
        "Spell 'spl_test_burn' { name = 'Test', circle = 1, mana = 5, kind = 'projectile', "
        "damage = { fire = 10 }, burn = true, fx = { trail = 'firebolt' } } "
        "Item 'it_rune_test_burn' { name = 'Test', category = 'rune', spell = 'spl_test_burn', value = 1 }");
    run(engine, "on('npc_burning', function(npc, caster) Story.burning = npc .. ' ' .. caster end)");
    run(engine, "give_item('it_rune_firebolt') equip('it_rune_firebolt') give_item('it_rune_test_burn') "
                "equip('it_rune_test_burn') set_talent('magic_circle', 1) set_stat('mana_max', 100) "
                "set_stat('mana', 100)");
    oldManAhead(engine);
    run(engine, "npc_set_stat('npc_old_man', 'hp_max', 100) npc_set_stat('npc_old_man', 'hp', 100)");
    CHECK(run(engine, "draw_magic()").asString() == "it_rune_firebolt");
    run(engine, "hero_rune(2)"); // the burning test spell
    runSeconds(engine, 1.0f);
    cast(engine); // 1.5 s after the cast: hit (10), burning about one second (5)
    CHECK(run(engine, "Story.burning").asString() == "npc_old_man hero");
    runSeconds(engine, 3.0f);
    CHECK(stat(engine, "npc_old_man", "hp") == 100 - 10 - 3 * 5); // three seconds of fire
    runSeconds(engine, 2.0f);
    CHECK(stat(engine, "npc_old_man", "hp") == 100 - 10 - 3 * 5); // out

    // The fire bolt does not set on fire (he is put back 12 m ahead: hit, he runs at the hero).
    run(engine, "Story.burning = nil hero_rune(1) npc_teleport('npc_old_man', 29.2, 0, 18.8, 270)");
    runSeconds(engine, 0.3f);
    run(engine, "npc_teleport('npc_old_man', 29.2, 0, 18.8, 270)");
    cast(engine);
    CHECK(stat(engine, "npc_old_man", "hp") == 75 - 25);
    CHECK(run(engine, "Story.burning").isNil());
}

TEST_CASE(
    "Engine NPC magic: the camp's herb witch and a bandit with a scroll cast fire bolts; the fear scroll "
    "(owner decisions A, C)")
{
    Engine engine(castConfig());
    REQUIRE(engine.init().ok());
    run(engine, "on('npc_cast', function(caster, spell) Story.casts = (Story.casts or '') .. caster .. ':' "
                ".. spell .. ' ' "
                "end)");
    run(engine,
        "Story.met_gate_guard = true teleport(41.2, 0, 18.8) set_stat('hp_max', 500) set_stat('hp', 500)");
    for (const char* npc : {"npc_camp_hexer", "npc_bandit"})
    {
        REQUIRE(run(engine, std::format("insert_npc('{}', 'wp_camp_center')", npc)).isString());
        run(engine, std::format("set_routine('{0}', '') npc_clear('{0}')", npc));
    }
    run(engine,
        "npc_teleport('npc_camp_hexer', 31.2, 0, 18.8, 270) npc_teleport('npc_bandit', 31.2, 0, 21.8, 270)");
    CHECK(run(engine, "npc_item_count('npc_bandit', 'it_scroll_firebolt')").asInteger() == 1);
    run(engine, "fight('npc_camp_hexer', 'hero') fight('npc_bandit', 'hero')");
    runSeconds(engine, 3.0f);
    const std::string casts(run(engine, "tostring(Story.casts)").asString());
    INFO(casts);
    CHECK(casts.find("npc_camp_hexer:spl_firebolt") != std::string::npos);
    CHECK(casts.find("npc_bandit:spl_firebolt") != std::string::npos); // read from his scroll (no circle)
    CHECK(run(engine, "npc_item_count('npc_bandit', 'it_scroll_firebolt')").asInteger() == 0); // used up

    // C: the fear scroll of the starting set.
    run(engine,
        "give_item('it_scroll_fear') equip('it_scroll_fear') set_stat('mana_max', 50) set_stat('mana', 50)");
    run(engine, "npc_teleport('npc_camp_hexer', 0, 0, 0, 0)"); // out of the way
    REQUIRE(run(engine, "insert_npc('npc_old_man', 'wp_camp_center')").isString());
    run(engine, "set_routine('npc_old_man', '') npc_clear('npc_old_man') npc_teleport('npc_old_man', 33.2, "
                "0, 18.8, 270) "
                "fight('npc_bandit', 'npc_old_man')");
    runSeconds(engine, 0.5f);
    CHECK(run(engine, "draw_magic()").asString() == "it_scroll_fear");
    runSeconds(engine, 1.0f);
    cast(engine);
    CHECK(run(engine, "item_count('it_scroll_fear')").asInteger() == 0);
    CHECK(stat(engine, "hero", "mana") == 40);
}
