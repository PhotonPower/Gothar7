// Scripts in the engine (M7 part C, headless): game/scripts load with the session, the console runs Lua,
// insert/goto/time/where act on the world, world_loaded and timers reach the scripts, a reload keeps Story.

#include <g7/physics/Character.hpp>
#include <g7/runtime/Engine.hpp>
#include <g7/world/WorldFile.hpp>

#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <format>
#include <ostream> // doctest needs it to print std::string operands
#include <string>

using namespace g7;

namespace
{
EngineConfig scriptConfig()
{
    EngineConfig config;
    config.appName = "scripts";
    config.headless = true;
    config.world = fs::fromUtf8("testworld/camp.g7world");
    config.start = "START_LAGER"; // feet at (34, 0, 0), looking west
    config.fixedFrameSeconds = 1.0 / 60.0;
    return config;
}

/// Runs a console line that must work; its value.
script::Value run(Engine& engine, std::string_view line)
{
    auto result = engine.runConsoleLine(line);
    const std::string what = std::string(line) + ": " + (result.ok() ? "" : result.error().message);
    REQUIRE_MESSAGE(result.ok(), what);
    return result.value();
}

bool printed(const Engine& engine, std::string_view text)
{
    const auto& lines = engine.consoleLines();
    return std::any_of(lines.begin(), lines.end(),
                       [&](const std::string& l) { return l.find(text) != std::string::npos; });
}
} // namespace

TEST_CASE("Engine scripts: content loads, the console acts on the world")
{
    Engine engine(scriptConfig());
    auto result = engine.init();
    REQUIRE_MESSAGE(result.ok(), (result.ok() ? "" : result.error().message));
    const script::ScriptVm* vm = engine.scripts();
    REQUIRE(vm != nullptr);
    std::string problems;
    for (const script::ScriptError& e : vm->errors())
    {
        problems += e.text() + "\n";
    }
    CHECK_MESSAGE(vm->errors().empty(), problems);
    CHECK(vm->instancesOf("Item").size() >= 6);
    CHECK(vm->instancesOf("Npc").size() >= 4);
    CHECK(vm->findInstance("Info", "dia_gate_guard_hello") != nullptr);
    // world_loaded reached the scripts.
    CHECK(printed(engine, "Welt geladen: testworld/camp.g7world (Besuch 1)"));
    REQUIRE(engine.player() != nullptr);

    // Expressions show their value; errors show with "!".
    CHECK(run(engine, "1 + 2").asInteger() == 3);
    CHECK(printed(engine, "> 1 + 2"));
    CHECK_FALSE(engine.runConsoleLine("insert('it_dragon')").ok());
    CHECK(printed(engine, "! console:1: insert: unknown instance \"it_dragon\""));

    // insert: items before the player, NPCs as animated figures facing it.
    CHECK(run(engine, "insert('it_apple', 3)").asBool());
    CHECK(run(engine, "insert('it_sword_old')").asBool());
    CHECK(engine.insertedItemCount() == 2);
    CHECK(run(engine, "insert('npc_gate_guard')").asBool());
    CHECK(engine.creatureCount() == 1);
    for (int i = 0; i < 10; ++i)
    {
        REQUIRE(engine.runFrame());
    }
    CHECK(engine.creatureState(1) == "move");
    const Vec3 npc = *engine.creaturePosition(1);
    CHECK(npc.x < 34.0f - 2.0f); // west of the player, who looks west
    CHECK(std::abs(npc.y) < 0.05f);

    // teleport (goto is a Lua keyword): start points and coordinates; time; where.
    CHECK(run(engine, "teleport('START_KLETTERPLATZ')").asBool());
    CHECK(engine.player()->feet().x == doctest::Approx(46.0f).epsilon(0.01));
    CHECK(engine.runConsoleLine("teleport(40, 0, -6)").ok());
    CHECK(engine.player()->feet().z == doctest::Approx(-6.0f).epsilon(0.01));
    CHECK_FALSE(engine.runConsoleLine("teleport('NOWHERE')").ok());
    CHECK(engine.runConsoleLine("time(12, 30)").ok());
    CHECK(engine.gameTime().minuteOfDay() == doctest::Approx(750.0).epsilon(0.001));
    CHECK_FALSE(engine.runConsoleLine("time(25)").ok());
    CHECK(run(engine, "where().world").asString() == "testworld/camp.g7world");
    CHECK(run(engine, "where().time").asString() == "12:30");

    // Story variables (dialogues set them; test_engine_dialog.cpp).
    run(engine, "Story.met_gate_guard = true");

    // Timers run in simulation time.
    CHECK(engine.runConsoleLine("after(0.1, function() Story.timer_fired = true end)").ok());
    CHECK(run(engine, "Story.timer_fired").isNil());
    for (int i = 0; i < 10; ++i)
    {
        REQUIRE(engine.runFrame());
    }
    CHECK(run(engine, "Story.timer_fired").asBool());

    // A reload keeps the story (scripts set Story.visits at load, the old value wins).
    engine.reloadScripts();
    CHECK(run(engine, "Story.met_gate_guard").asBool());
    CHECK(run(engine, "Story.visits").asInteger() == 1);
    CHECK(engine.scripts()->errors().empty());
}

TEST_CASE("Engine scripts: docs/script-api.md lists the engine functions and events")
{
    Engine engine(scriptConfig());
    REQUIRE(engine.init().ok());
    const std::string md = engine.scriptApiMarkdown();
    for (const char* entry :
         {"### `insert(instance: string, count?: integer) -> boolean`", "## Welt", "## Ereignisse",
          "### `on(\"world_loaded\", fn(world: string))`", "### `time(hour: integer, minute?: integer)`"})
    {
        CAPTURE(entry);
        CHECK(md.find(entry) != std::string::npos);
    }
}

TEST_CASE("Engine scripts: the hero's values, inventory, equipment and levels")
{
    Engine engine(scriptConfig());
    auto result = engine.init();
    REQUIRE_MESSAGE(result.ok(), (result.ok() ? "" : result.error().message));
    REQUIRE(engine.hero() != nullptr);
    CHECK(engine.hero()->instance() == "pc_hero");
    CHECK(run(engine, "hero().level").asInteger() == 0);
    CHECK(run(engine, "hero().next_xp").asInteger() == 500);
    CHECK(run(engine, "stat('hp')").asInteger() == 40);

    // Items in and out; unknown items are an error naming them.
    CHECK(run(engine, "give_item('it_apple', 3)").asInteger() == 5);
    CHECK(run(engine, "item_count('it_apple')").asInteger() == 5);
    CHECK(run(engine, "remove_item('it_apple', 2)").asBool());
    CHECK_FALSE(run(engine, "remove_item('it_apple', 10)").asBool());
    const auto unknown = engine.runConsoleLine("give_item('it_dragon')");
    REQUIRE_FALSE(unknown.ok());
    CHECK(unknown.error().message.find("unknown item \"it_dragon\"") != std::string::npos);

    // Equipment: requirements, slot names, protection with equipment.
    run(engine, "give_item('it_armor_leather')");
    const auto weak = engine.runConsoleLine("equip('it_armor_leather')");
    REQUIRE_FALSE(weak.ok());
    CHECK(weak.error().message.find("needs str 15") != std::string::npos);
    run(engine, "set_stat('str', 20)");
    CHECK(run(engine, "equip('it_armor_leather')").asString() == "armor");
    CHECK(run(engine, "equipped('armor')").asString() == "it_armor_leather");
    CHECK(run(engine, "stat('protection_edge')").asInteger() == 15);
    run(engine, "unequip('armor')");
    CHECK(run(engine, "equipped('armor')").isNil());
    CHECK(run(engine, "inventory()[1].item").asString() == "it_armor_leather"); // armour before food

    // Talents and levels; level_up reaches the scripts.
    run(engine, "set_talent('picklock', 1)");
    CHECK(run(engine, "talent('picklock')").asInteger() == 1);
    run(engine, "on('level_up', function(level) Story.last_level = level end)");
    CHECK(run(engine, "add_xp(1600)").asInteger() == 2);
    CHECK(run(engine, "Story.last_level").asInteger() == 2);
    CHECK(run(engine, "hero().learn_points").asInteger() == 20);
    CHECK(run(engine, "attitude('guard', 'outcast')").asString() == "hostile");
    CHECK(run(engine, "attitude('farmer', 'farmer')").asString() == "friendly");

    // A reload keeps the hero's state.
    engine.reloadScripts();
    CHECK(run(engine, "item_count('it_apple')").asInteger() == 3);
    CHECK(run(engine, "hero().level").asInteger() == 2);
}

TEST_CASE("Engine items: focus, picking up, dropping, the inventory stops the hero")
{
    Engine engine(scriptConfig());
    auto result = engine.init();
    REQUIRE_MESSAGE(result.ok(), (result.ok() ? "" : result.error().message));
    REQUIRE(engine.hero() != nullptr);
    const u32 apples = engine.hero()->itemCount("it_apple");
    // The camp has items of its own (vob type item), not in view from the start point.
    const usize lying = engine.worldItems().size();
    CHECK(lying == 9);
    engine.updateFocus();
    CHECK_FALSE(engine.focus().has_value());

    // insert puts an item vob 1.2 m in front of the hero: it comes into focus with its name.
    run(engine, "on('item_taken', function(item, count) Story.taken = item .. ' ' .. count end)");
    REQUIRE(run(engine, "insert('it_apple')").asBool());
    REQUIRE(engine.worldItems().size() == lying + 1);
    const WorldItemInfo inserted = engine.worldItems().back();
    CHECK(inserted.instance == "it_apple");
    CHECK(inserted.vob.runtime());
    engine.updateFocus();
    REQUIRE(engine.focus().has_value());
    CHECK(engine.focus()->kind == gameplay::FocusKind::Item);
    CHECK(engine.focus()->id == inserted.vob.value);
    CHECK(engine.focus()->name == "Apfel");

    // Picking up: the hero stands still, after a moment the apple is in the bag and gone from the world.
    REQUIRE(engine.pickUpFocus().ok());
    CHECK_FALSE(engine.pickUpFocus().ok()); // busy
    CHECK(engine.pickingUp());
    bool bent = false; // the figure plays none/t_pickup_ground; the apple goes at its event
    bool takenEarly = false;
    for (int i = 0; i < 300 && engine.pickingUp(); ++i) // the clip takes ~2 s, its event comes at 1.2 s
    {
        REQUIRE(engine.runFrame());
        bent = bent || engine.playerAnimationState() == "pickup";
        takenEarly = takenEarly || (i < 30 && engine.hero()->itemCount("it_apple") > apples);
    }
    CHECK(bent);
    CHECK_FALSE(takenEarly);
    CHECK_FALSE(engine.pickingUp());
    CHECK(engine.hero()->itemCount("it_apple") == apples + 1);
    CHECK(engine.worldItems().size() == lying);
    CHECK_FALSE(engine.focus().has_value());
    CHECK(run(engine, "Story.taken").asString() == "it_apple 1");
    CHECK_FALSE(engine.pickUpFocus().ok()); // nothing in focus

    // Dropping puts it in front of him again; turning away loses the focus.
    REQUIRE(engine.dropItem("it_apple").ok());
    CHECK(engine.hero()->itemCount("it_apple") == apples);
    REQUIRE(engine.worldItems().size() == lying + 1);
    engine.updateFocus();
    CHECK(engine.focus().has_value());
    CHECK_FALSE(engine.dropItem("it_dragon").ok());

    // Inventory open: the hero does not walk.
    engine.setInventoryOpen(true);
    CHECK(engine.inventoryOpen());
    const Vec3 before = engine.player()->feet();
    gameplay::MoveInput forward;
    forward.forward = 1.0f;
    engine.setPlayerInputOverride(forward);
    for (int i = 0; i < 20; ++i)
    {
        REQUIRE(engine.runFrame());
    }
    CHECK(glm::length(engine.player()->feet() - before) < 0.01f);
    engine.setInventoryOpen(false);
    for (int i = 0; i < 20; ++i)
    {
        REQUIRE(engine.runFrame());
    }
    CHECK(glm::length(engine.player()->feet() - before) > 0.1f);
}

namespace
{
/// Runs frames until the hero is done with the mob (or the limit); whether `phase` was seen on the way.
bool runMobUse(Engine& engine, std::string_view phase = {}, int limit = 600)
{
    bool seen = phase.empty();
    for (int i = 0; i < limit && engine.mobPhase(); ++i)
    {
        REQUIRE(engine.runFrame());
        seen = seen || engine.mobPhase() == phase;
    }
    return seen && !engine.mobPhase();
}

/// Runs frames until the mob use reaches `phase`.
bool runUntilPhase(Engine& engine, std::string_view phase, int limit = 600)
{
    for (int i = 0; i < limit && engine.mobPhase() && engine.mobPhase() != phase; ++i)
    {
        REQUIRE(engine.runFrame());
    }
    return engine.mobPhase() == phase;
}
} // namespace

TEST_CASE("Engine mobs: chests open and close, take and put, locks with key and lockpick")
{
    Engine engine(scriptConfig());
    auto result = engine.init();
    REQUIRE_MESSAGE(result.ok(), (result.ok() ? "" : result.error().message));
    const auto chest = engine.findMob("LAGER_TRUHE");
    REQUIRE(chest.has_value());
    const MobInfo info = *engine.mobInfo(*chest);
    CHECK(info.type == "chest");
    CHECK(info.name == "Truhe");
    CHECK_FALSE(info.locked);
    CHECK(info.contents.size() == 3);

    // Walk over, open (chest_enter with the lid's "open" event), its contents next to the inventory.
    REQUIRE(engine.useMob(*chest).ok());
    CHECK(engine.mobPhase() == "approach");
    CHECK_FALSE(engine.useMob(*chest).ok()); // busy
    REQUIRE(runUntilPhase(engine, "loop"));
    CHECK(engine.playerAnimationState() == "chest_loop");
    CHECK(engine.mobInfo(*chest)->open);
    CHECK(engine.inventoryOpen());
    // The hero stands on the slot in front of it: 0.65 m before the chest along its front (+X here).
    const Vec3 feet = engine.player()->feet();
    CHECK(feet.x == doctest::Approx(29.65f).epsilon(0.01));
    CHECK(feet.z == doctest::Approx(-4.0f).epsilon(0.01));
    const u32 apples = engine.hero()->itemCount("it_apple");
    REQUIRE(engine.takeFromMob(*chest, "it_apple", 2).ok());
    CHECK(engine.hero()->itemCount("it_apple") == apples + 2);
    CHECK_FALSE(engine.takeFromMob(*chest, "it_apple").ok()); // none left
    REQUIRE(engine.putIntoMob(*chest, "it_apple").ok());
    CHECK(engine.mobInfo(*chest)->contents.size() == 3);
    // Leaving closes it (chest_leave) and gives the hero back.
    engine.mobCommand(MobCommand::Leave);
    CHECK(runMobUse(engine, "leave"));
    CHECK_FALSE(engine.mobInfo(*chest)->open);
    CHECK_FALSE(engine.inventoryOpen());

    // Locked, no key, no lockpick: "Verschlossen.", nothing happens.
    const auto locked = engine.findMob("LAGER_TRUHE_ZU");
    REQUIRE(locked.has_value());
    CHECK(engine.mobInfo(*locked)->locked);
    run(engine, "on('mob_locked', function(mob) Story.locked = mob end)");
    const auto refused = engine.useMob(*locked);
    REQUIRE_FALSE(refused.ok());
    CHECK(refused.error().message == "locked");
    CHECK(engine.lastNotice() == "Verschlossen.");
    CHECK(run(engine, "Story.locked").asString() == "mob_camp_chest_locked");

    // With two lockpicks: a wrong turn with a bad roll breaks one (no talent: 50 %), the right combination
    // (LRRLR) opens it, then it opens like any chest.
    run(engine, "give_item('it_lockpick', 2)");
    f32 roll = 0.1f; // < 0.5: breaks
    engine.setRandomSource([&] { return roll; });
    REQUIRE(engine.useMob(*locked).ok());
    REQUIRE(runUntilPhase(engine, "picklock"));
    CHECK(engine.playerAnimationState() == "chest_picklock");
    engine.mobCommand(MobCommand::TurnRight); // wrong first step
    CHECK(engine.hero()->itemCount("it_lockpick") == 1);
    CHECK(engine.lastNotice() == "Der Dietrich ist abgebrochen.");
    roll = 0.9f;
    engine.mobCommand(MobCommand::TurnLeft);
    engine.mobCommand(MobCommand::TurnLeft); // wrong again: reset, the pick holds
    CHECK(engine.hero()->itemCount("it_lockpick") == 1);
    for (const MobCommand c : {MobCommand::TurnLeft, MobCommand::TurnRight, MobCommand::TurnRight,
                               MobCommand::TurnLeft, MobCommand::TurnRight})
    {
        engine.mobCommand(c);
    }
    CHECK_FALSE(engine.mobInfo(*locked)->locked);
    CHECK(engine.lastNotice() == "Das Schloss springt auf.");
    REQUIRE(runUntilPhase(engine, "loop"));
    REQUIRE(engine.takeFromMob(*locked, "it_ring_protection").ok());
    engine.mobCommand(MobCommand::Leave);
    CHECK(runMobUse(engine));

    // The locked door opens with its key on the way (without key and lockpick it stays shut).
    run(engine, "remove_item('it_lockpick', item_count('it_lockpick'))");
    const auto door = engine.findMob("LAGER_TUER_ZU");
    REQUIRE(door.has_value());
    CHECK_FALSE(engine.useMob(*door).ok());
    run(engine, "give_item('it_key_hut_door')");
    REQUIRE(engine.useMob(*door).ok());
    CHECK_FALSE(engine.mobInfo(*door)->locked);
    CHECK(runMobUse(engine));
    CHECK(engine.mobInfo(*door)->open);
}

TEST_CASE("Engine mobs: a door swings open with its collision, the same clip closes it")
{
    Engine engine(scriptConfig());
    auto result = engine.init();
    REQUIRE_MESSAGE(result.ok(), (result.ok() ? "" : result.error().message));
    const auto door = engine.findMob("LAGER_TUER");
    REQUIRE(door.has_value());
    const auto blade = [&]
    {
        for (const SceneInstance& instance : engine.instances())
        {
            if (instance.vob == *door)
            {
                return Vec3(instance.transform[0]); // the blade runs along local +X
            }
        }
        return Vec3(0.0f);
    };
    CHECK(blade().x == doctest::Approx(1.0f));
    run(engine, "on('mob_used', function(mob, type) Story.used = mob .. ' ' .. type end)");
    REQUIRE(engine.useMob(*door).ok());
    CHECK(runMobUse(engine, "enter"));
    CHECK(engine.mobInfo(*door)->open);
    CHECK(run(engine, "Story.used").asString() == "mob_camp_door door");
    for (int i = 0; i < 60; ++i) // the swing takes 0.8 s
    {
        REQUIRE(engine.runFrame());
    }
    // Turned 90 degrees about +Y: the blade now runs along -Z.
    CHECK(blade().x == doctest::Approx(0.0f).epsilon(0.01));
    CHECK(blade().z == doctest::Approx(-1.0f).epsilon(0.01));
    // Where the closed blade stood (x 30.5..31.5 at z -9) is free now; the open one lies along -Z at x 30.5.
    CHECK(engine.physics().raycast(Vec3(31.0f, 1.0f, -8.0f), Vec3(0, 0, -1), 2.0f,
                                   physics::layerBit(physics::Layer::World)) == std::nullopt);
    REQUIRE(engine.useMob(*door).ok());
    CHECK(runMobUse(engine));
    for (int i = 0; i < 60; ++i)
    {
        REQUIRE(engine.runFrame());
    }
    CHECK_FALSE(engine.mobInfo(*door)->open);
    CHECK(blade().x == doctest::Approx(1.0f).epsilon(0.01));
    CHECK(
        engine.physics()
            .raycast(Vec3(31.0f, 1.0f, -8.0f), Vec3(0, 0, -1), 2.0f, physics::layerBit(physics::Layer::World))
            .has_value());
}

TEST_CASE("Engine mobs: forging at the anvil, sleeping in the bed")
{
    Engine engine(scriptConfig());
    auto result = engine.init();
    REQUIRE_MESSAGE(result.ok(), (result.ok() ? "" : result.error().message));

    // Anvil: the recipe needs a glowing blank; three blows (hit_anvil of s_work), then the sword.
    const auto anvil = engine.findMob("LAGER_AMBOSS");
    REQUIRE(anvil.has_value());
    run(engine, "on('item_crafted', function(recipe) Story.crafted = recipe end)");
    REQUIRE(engine.useMob(*anvil).ok());
    REQUIRE(runUntilPhase(engine, "loop"));
    CHECK(engine.playerAnimationState() == "anvil_loop");
    REQUIRE(engine.mobChoices() == std::vector<std::string>{"Grobes Schwert"});
    const auto missing = engine.chooseMobOption(0);
    REQUIRE_FALSE(missing.ok());
    CHECK(missing.error().message == "Dafür fehlt: Glühender Rohling.");
    run(engine, "give_item('it_blank_hot')");
    REQUIRE(engine.chooseMobOption(0).ok());
    CHECK(engine.mobChoices().empty()); // busy forging
    for (int i = 0; i < 600 && engine.hero()->itemCount("it_sword_crude") == 0; ++i)
    {
        REQUIRE(engine.runFrame());
    }
    CHECK(engine.hero()->itemCount("it_sword_crude") == 1);
    CHECK(engine.hero()->itemCount("it_blank_hot") == 0);
    CHECK(run(engine, "Story.crafted").asString() == "rcp_sword_crude");
    CHECK(engine.mobChoices().size() == 1); // again
    engine.mobCommand(MobCommand::Leave);
    CHECK(runMobUse(engine, "leave"));

    // Bed: at 22:00 hurt, sleep until morning: day after, 08:00, hit points full; he gets up by himself.
    run(engine, "time(22, 0)");
    run(engine, "set_stat('hp', 5)");
    const u32 day = engine.gameTime().day();
    run(engine, "on('slept', function(hour) Story.slept = hour end)");
    const auto bed = engine.findMob("LAGER_BETT");
    REQUIRE(bed.has_value());
    REQUIRE(engine.useMob(*bed).ok());
    REQUIRE(runUntilPhase(engine, "loop"));
    CHECK(engine.playerAnimationState() == "bed_loop");
    REQUIRE(engine.mobChoices().size() == 4);
    CHECK(engine.mobChoices()[0] == "Bis zum Morgen (8:00)");
    REQUIRE(engine.chooseMobOption(0).ok());
    CHECK(engine.gameTime().day() == day + 1);
    CHECK(engine.gameTime().hourOfDay() == doctest::Approx(8.0f).epsilon(0.01));
    CHECK(engine.hero()->attribute("hp") == engine.hero()->attribute("hp_max"));
    CHECK(run(engine, "Story.slept").asInteger() == 8);
    CHECK(runMobUse(engine, "leave"));
}

TEST_CASE("Engine mobs: the spots of the PR play guide face their mobs")
{
    Engine engine(scriptConfig());
    auto result = engine.init();
    REQUIRE_MESSAGE(result.ok(), (result.ok() ? "" : result.error().message));
    constexpr f32 kWest = 1.5707963f; // yaw: 0 = -Z, positive turns left
    const auto focusFrom = [&](const char* teleport, f32 yaw)
    {
        run(engine, teleport);
        engine.steerPlayer(yaw);
        REQUIRE(engine.runFrame());
        engine.updateFocus();
        return engine.focus() ? engine.focus()->name : std::string();
    };
    CHECK(focusFrom("teleport(31, 0, -4.8)", kWest).find("Truhe") != std::string::npos);
    CHECK(focusFrom("teleport(31, 0, -1.5)", kWest) == "Amboss");
    CHECK(focusFrom("teleport(28, 0, -4.5)", kWest) == "Bett");
    CHECK(focusFrom("teleport(31, 0, -7.5)", 0.0f).find("Tür") != std::string::npos); // facing south (-Z)
}

TEST_CASE("Engine use: eating, drinking, reading - only standing, effects at the use event")
{
    Engine engine(scriptConfig());
    auto result = engine.init();
    REQUIRE_MESSAGE(result.ok(), (result.ok() ? "" : result.error().message));
    for (int i = 0; i < 20; ++i) // the hero lands on the start point
    {
        REQUIRE(engine.runFrame());
    }
    const auto runWhile = [&](auto busy)
    {
        for (int i = 0; i < 400 && busy(); ++i)
        {
            REQUIRE(engine.runFrame());
        }
    };
    run(engine, "set_stat('hp', 10)");
    run(engine, "on('item_used', function(item) Story.used = item end)");
    const u32 apples = engine.hero()->itemCount("it_apple");
    REQUIRE(engine.useItem("it_apple").ok());
    CHECK(engine.usingItem());
    CHECK(engine.hero()->attribute("hp") == 10); // not before the "use" event of t_eat
    bool eating = false;
    for (int i = 0; i < 400 && engine.usingItem(); ++i)
    {
        REQUIRE(engine.runFrame());
        eating = eating || engine.playerAnimationState() == "use_eat";
    }
    CHECK(eating);
    CHECK(engine.hero()->attribute("hp") == 15); // apple +5 (owner's value)
    CHECK(engine.hero()->itemCount("it_apple") == apples - 1);
    CHECK(run(engine, "Story.used").asString() == "it_apple");

    // A potion: +40, capped at the maximum.
    run(engine, "give_item('it_potion_heal_small')");
    REQUIRE(engine.useItem("it_potion_heal_small").ok());
    runWhile([&] { return engine.usingItem(); });
    CHECK(engine.hero()->attribute("hp") == engine.hero()->attribute("hp_max"));
    CHECK(engine.hero()->itemCount("it_potion_heal_small") == 0);

    // A document opens and stays in the bag.
    run(engine, "give_item('it_letter_farm')");
    REQUIRE(engine.useItem("it_letter_farm").ok());
    runWhile([&] { return engine.usingItem(); });
    REQUIRE(engine.document().has_value());
    CHECK(engine.document()->title == "Brief an den Bauern");
    CHECK(engine.document()->text.find("Vollmond") != std::string::npos);
    CHECK(engine.hero()->itemCount("it_letter_farm") == 1);
    engine.closeDocument();
    CHECK_FALSE(engine.document().has_value());

    // Not usable; not while running.
    CHECK_FALSE(engine.useItem("it_club").ok());
    CHECK_FALSE(engine.useItem("it_dragon").ok());
    run(engine, "give_item('it_bread')");
    gameplay::MoveInput forward;
    forward.forward = 1.0f;
    engine.setPlayerInputOverride(forward);
    for (int i = 0; i < 30; ++i)
    {
        REQUIRE(engine.runFrame());
    }
    CHECK_FALSE(engine.useItem("it_bread").ok());
    CHECK(engine.lastNotice() == "Nicht jetzt.");
}

TEST_CASE("Engine use: beer is drunk, the strength elixir lasts, the speed potion runs faster for a while")
{
    Engine engine(scriptConfig());
    auto result = engine.init();
    REQUIRE_MESSAGE(result.ok(), (result.ok() ? "" : result.error().message));
    for (int i = 0; i < 20; ++i)
    {
        REQUIRE(engine.runFrame());
    }
    const auto use = [&](const char* item)
    {
        run(engine, std::format("give_item('{}')", item));
        REQUIRE(engine.useItem(item).ok());
        std::string clip;
        for (int i = 0; i < 400 && engine.usingItem(); ++i)
        {
            REQUIRE(engine.runFrame());
            if (engine.playerAnimationState().starts_with("use_"))
            {
                clip = std::string(engine.playerAnimationState());
            }
        }
        return clip;
    };
    // Beer and wine (food tagged "drink") are drunk, ham eaten.
    CHECK(use("it_beer") == "use_drink");
    CHECK(use("it_ham") == "use_eat");
    // The strength elixir: +3 for good (owner).
    const i32 strength = engine.hero()->attribute("str");
    CHECK(use("it_potion_strength") == "use_drink");
    CHECK(engine.hero()->attribute("str") == strength + 3);

    // The speed potion: x1.3 for 120 s (owner).
    const auto speed = [&]
    {
        gameplay::MoveInput forward;
        forward.forward = 1.0f;
        engine.setPlayerInputOverride(forward);
        for (int i = 0; i < 60; ++i) // up to speed
        {
            REQUIRE(engine.runFrame());
        }
        const Vec3 from = engine.player()->feet();
        for (int i = 0; i < 60; ++i)
        {
            REQUIRE(engine.runFrame());
        }
        const Vec3 to = engine.player()->feet();
        engine.setPlayerInputOverride(std::nullopt);
        for (int i = 0; i < 60; ++i) // stand again
        {
            REQUIRE(engine.runFrame());
        }
        return glm::length(Vec2(to.x - from.x, to.z - from.z));
    };
    run(engine, "teleport(40, 0, 30)");
    const f32 normal = speed();
    run(engine, "teleport(40, 0, 30)");
    for (int i = 0; i < 30; ++i) // landed: using needs standing
    {
        REQUIRE(engine.runFrame());
    }
    CHECK(use("it_potion_speed") == "use_drink");
    REQUIRE_FALSE(run(engine, "hero_boost()").isNil());
    CHECK(run(engine, "hero_boost().speed").asNumber() == doctest::Approx(1.3));
    CHECK(run(engine, "hero_boost().seconds").asNumber() > 115.0);
    const f32 boosted = speed();
    MESSAGE("running ", normal, " m/s, with the potion ", boosted, " m/s");
    CHECK(boosted == doctest::Approx(normal * 1.3f).epsilon(0.05));
    // Its end (a short potion of the same kind, defined here).
    run(engine, "on('boost_ended', function() Story.boost_ended = true end)");
    run(engine,
        "Item 'it_test_boost' { name = 'Test', category = 'potion', boost = { speed = 1.3, seconds = 1 } }");
    use("it_test_boost");
    for (int i = 0; i < 90; ++i)
    {
        REQUIRE(engine.runFrame());
    }
    CHECK(run(engine, "hero_boost()").isNil());
    CHECK(run(engine, "Story.boost_ended").asBool());
}

TEST_CASE("Engine use: pickpocketing as in Gothic 1 - talent, then dexterity, once per NPC")
{
    Engine engine(scriptConfig());
    auto result = engine.init();
    REQUIRE_MESSAGE(result.ok(), (result.ok() ? "" : result.error().message));
    for (int i = 0; i < 20; ++i) // the hero lands on the start point
    {
        REQUIRE(engine.runFrame());
    }
    const auto steal = [&]
    {
        engine.updateFocus();
        auto tried = engine.pickpocketFocus();
        for (int i = 0; i < 400 && engine.pickpocketing(); ++i)
        {
            REQUIRE(engine.runFrame());
        }
        return tried;
    };
    run(engine, "on('pickpocket', function(npc, item) Story.stolen = item end)");
    run(engine, "on('pickpocket_failed', function(npc) Story.noticed = npc end)");

    // The farmer woman 2.5 m in front of the hero (needs dexterity 15).
    REQUIRE(run(engine, "insert('npc_farmer_woman')").asBool());
    engine.updateFocus();
    REQUIRE(engine.focus().has_value());
    REQUIRE(engine.focus()->kind == gameplay::FocusKind::Npc);
    CHECK(engine.focus()->name == "Bäuerin");
    const u32 id = static_cast<u32>(engine.focus()->id);
    REQUIRE(engine.creatureInventory(id).has_value());
    CHECK(engine.creatureInventory(id)->size() == 3);

    // Without the talent: no.
    CHECK_FALSE(steal().ok());
    CHECK(engine.lastNotice() == "Das kann ich nicht.");
    // Talent, but dexterity 10 < 15: she notices; only one try.
    run(engine, "set_talent('pickpocket', 1)");
    REQUIRE(steal().ok());
    CHECK(run(engine, "Story.noticed").asString() == "npc_farmer_woman");
    CHECK(engine.creatureInventory(id)->size() == 3);
    CHECK_FALSE(steal().ok());
    CHECK(engine.lastNotice() == "Da ist nichts mehr zu holen.");

    // Another one with enough dexterity: one item changes hands for sure.
    engine.removeCreatures();
    REQUIRE(run(engine, "insert('npc_farmer_woman')").asBool());
    run(engine, "set_stat('dex', 20)");
    engine.setRandomSource([] { return 0.0f; });
    const u32 apples = engine.hero()->itemCount("it_apple");
    REQUIRE(steal().ok());
    CHECK(run(engine, "Story.stolen").asString() == "it_apple"); // first stack (food before documents)
    CHECK(engine.hero()->itemCount("it_apple") == apples + 1);
}

TEST_CASE("Engine use: taking somebody else's things is theft")
{
    Engine engine(scriptConfig());
    auto result = engine.init();
    REQUIRE_MESSAGE(result.ok(), (result.ok() ? "" : result.error().message));
    run(engine,
        "on('theft', function(owner, item, count) Story.theft = owner .. ' ' .. item .. ' ' .. count end)");
    // An item lying in front of the hero that belongs to the guards.
    REQUIRE(run(engine, "insert('it_bread')").asBool());
    const WorldItemInfo bread = engine.worldItems().back();
    engine.scene().get<world::ItemRef>(engine.scene().findById(bread.vob))->owner = "guard";
    engine.updateFocus();
    REQUIRE(engine.pickUpFocus().ok());
    for (int i = 0; i < 300 && engine.pickingUp(); ++i)
    {
        REQUIRE(engine.runFrame());
    }
    CHECK(run(engine, "Story.theft").asString() == "guard it_bread 1");
    // owned_by: the locked chest of the camp belongs to the farmer woman.
    CHECK(run(engine, "owned_by('LAGER_TRUHE_ZU')").asString() == "npc_farmer_woman");
    CHECK(run(engine, "owned_by('LAGER_TRUHE')").isNil());
}

TEST_CASE("Engine NPCs: walking over the camp's waynet through the gate to the fire and back")
{
    Engine engine(scriptConfig());
    auto result = engine.init();
    REQUIRE_MESSAGE(result.ok(), (result.ok() ? "" : result.error().message));
    REQUIRE_FALSE(engine.waynet().empty());
    CHECK(engine.waynet().find("wp_camp_fire").has_value()); // names without regard to case

    run(engine, "on('npc_arrived', function(npc, target) Story.arrived = npc .. ' ' .. target end)");
    REQUIRE(run(engine, "insert('npc_farmer_woman')").asBool());
    const auto id = engine.npcByInstance("npc_farmer_woman");
    REQUIRE(id.has_value());
    for (int i = 0; i < 30; ++i) // she lands
    {
        REQUIRE(engine.runFrame());
    }
    CHECK_FALSE(engine.npcGoTo(*id, "wp_nowhere").ok());
    run(engine, "set_routine('npc_farmer_woman', '') npc_clear('npc_farmer_woman')"); // only what we say
    run(engine, "npc_goto('npc_farmer_woman', 'wp_camp_fire')"); // queued: starts with the next step
    REQUIRE(engine.runFrame());
    CHECK(engine.npcWalking(*id));
    // ~33 m at walking pace: about 21 s.
    for (int i = 0; i < 60 * 45 && engine.npcWalking(*id); ++i)
    {
        REQUIRE(engine.runFrame());
    }
    CHECK_FALSE(engine.npcWalking(*id));
    CHECK(run(engine, "Story.arrived").asString() == "npc_farmer_woman wp_camp_fire");
    const Vec3 fire = engine.waynet().points()[*engine.waynet().find("WP_CAMP_FIRE")].position;
    const Vec3 at = *engine.creaturePosition(*id);
    CHECK(glm::length(Vec3(at.x - fire.x, 0.0f, at.z - fire.z)) < 0.5f);

    // Back out, running, to a freepoint at the gate.
    REQUIRE(engine.npcGoTo(*id, "fp_stand_gate_01", true).ok());
    for (int i = 0; i < 60 * 30 && engine.npcWalking(*id); ++i)
    {
        REQUIRE(engine.runFrame());
    }
    CHECK_FALSE(engine.npcWalking(*id));
    CHECK(run(engine, "Story.arrived").asString() == "npc_farmer_woman fp_stand_gate_01");

    // welt #227: the player goes out of range while she walks. She does not stop silently on the way: she is
    // put at her goal and arrives.
    run(engine, "Story.arrived = nil");
    REQUIRE(engine.npcGoTo(*id, "wp_camp_fire").ok());
    REQUIRE(engine.runFrame());
    REQUIRE(engine.npcWalking(*id));
    run(engine, "teleport(400, 0, 400)");
    for (int i = 0; i < 10; ++i)
    {
        REQUIRE(engine.runFrame());
    }
    CHECK_FALSE(engine.npcWalking(*id));
    CHECK(run(engine, "Story.arrived").asString() == "npc_farmer_woman wp_camp_fire");
    const Vec3 there = *engine.creaturePosition(*id);
    CHECK(glm::length(Vec3(there.x - fire.x, 0.0f, there.z - fire.z)) < 0.5f);
}

namespace
{
std::string npcState(Engine& engine, std::string_view npc)
{
    return std::string(run(engine, std::format("npc_state('{}').state", npc)).asString());
}

std::string npcAmbient(Engine& engine, std::string_view npc)
{
    return std::string(run(engine, std::format("npc_state('{}').ambient", npc)).asString());
}

void runSeconds(Engine& engine, f32 seconds)
{
    for (int i = 0; i < static_cast<int>(seconds * 60.0f); ++i)
    {
        REQUIRE(engine.runFrame());
    }
}
} // namespace

TEST_CASE("Engine NPC routines: the state of the time's entry, freepoints, ambient animations, AI LOD")
{
    Engine engine(scriptConfig());
    auto result = engine.init();
    REQUIRE_MESSAGE(result.ok(), (result.ok() ? "" : result.error().message));
    run(engine, "time(20, 0)");

    // insert_npc without a place: where the routine wants her now (19-23: sitting at the fire).
    REQUIRE(run(engine, "insert_npc('npc_farmer_woman')").isString());
    const auto woman = engine.npcByInstance("npc_farmer_woman");
    REQUIRE(woman.has_value());
    runSeconds(engine, 1.0f);
    CHECK(npcState(engine, "npc_farmer_woman") == "zs_sit_campfire");
    // She walks to a free SIT freepoint, turns and sits down (in, loop).
    runSeconds(engine, 20.0f);
    CHECK_FALSE(engine.npcWalking(*woman));
    CHECK(npcAmbient(engine, "npc_farmer_woman") == "sit_ground");
    const Vec3 at = *engine.creaturePosition(*woman);
    const auto& fps = engine.waynet().freepoints();
    const bool onSeat = std::ranges::any_of(
        fps, [&](const auto& fp) { return fp.type == "SIT" && glm::length(fp.position - at) < 0.6f; });
    CHECK(onSeat);

    // The woodcutter at the same time wants to sit too: he takes the other seat, not hers.
    REQUIRE(run(engine, "insert_npc('npc_woodcutter', 'wp_camp_center')").isString());
    const auto woodcutter = engine.npcByInstance("npc_woodcutter");
    runSeconds(engine, 20.0f);
    CHECK(npcAmbient(engine, "npc_woodcutter") == "sit_ground");
    const Vec3 his = *engine.creaturePosition(*woodcutter);
    CHECK(glm::length(his - *engine.creaturePosition(*woman)) > 1.0f);

    // At 23:00 her routine says: sleep at the west way point. She stands up first (out) and walks there.
    run(engine, "time(23, 0)");
    runSeconds(engine, 2.0f);
    CHECK(npcState(engine, "npc_farmer_woman") == "zs_sleep");
    runSeconds(engine, 25.0f);
    CHECK(npcAmbient(engine, "npc_farmer_woman") == "sleep_ground");
    const Vec3 west = engine.waynet().points()[*engine.waynet().find("wp_camp_west")].position;
    CHECK(glm::length(Vec3(*engine.creaturePosition(*woman) - west) * Vec3(1, 0, 1)) < 0.6f);

    // A script interrupts with another state; when that ends ("done"), the routine takes over again.
    run(engine, "npc_start_state('npc_farmer_woman', 'zs_look_around')");
    CHECK(npcState(engine, "npc_farmer_woman") == "zs_look_around");
    runSeconds(engine, 7.0f); // standing up 1.5 s, looking around 3 s
    INFO(run(engine, "local s = npc_state('npc_farmer_woman') return s.animation .. ' ' .. s.commands .. ' ' "
                     ".. s.ambient")
             .asString());
    CHECK(npcState(engine, "npc_farmer_woman") == "zs_sleep"); // the routine again

    // AI LOD: the player far away -> she is no longer simulated; a routine change moves her straight there.
    run(engine, "teleport(400, 0, 400)");
    runSeconds(engine, 1.0f);
    run(engine, "time(13, 0)"); // 12-19: warming at the fire
    runSeconds(engine, 2.0f);
    CHECK(npcState(engine, "npc_farmer_woman") == "zs_campfire");
    const Vec3 fire = engine.waynet().points()[*engine.waynet().find("wp_camp_fire")].position;
    CHECK(glm::length(Vec3(*engine.creaturePosition(*woman) - fire) * Vec3(1, 0, 1)) < 1.0f);
    CHECK(npcAmbient(engine, "npc_farmer_woman").empty()); // nothing begun while far away
}

TEST_CASE("Engine NPC commands: errors and the queue")
{
    Engine engine(scriptConfig());
    REQUIRE(engine.init().ok());
    CHECK_FALSE(engine.runConsoleLine("npc_play('npc_nobody', 'guard')").ok());
    CHECK_FALSE(engine.runConsoleLine("insert_npc('npc_farmer_woman', 'wp_nowhere')").ok());
    REQUIRE(run(engine, "insert_npc('npc_old_man', 'wp_camp_south')").isString());
    CHECK_FALSE(engine.runConsoleLine("set_routine('npc_old_man', 'rtn_nowhere')").ok());
    CHECK_FALSE(engine.runConsoleLine("npc_start_state('npc_old_man', 'zs_nowhere')").ok());
    run(engine, "set_routine('npc_old_man', '') npc_clear('npc_old_man')");
    run(engine, "npc_play('npc_old_man', 'guard') npc_wait('npc_old_man', 1) npc_stop('npc_old_man')");
    runSeconds(engine, 0.5f);
    CHECK(npcAmbient(engine, "npc_old_man") == "guard");
    runSeconds(engine, 4.0f);
    CHECK(npcAmbient(engine, "npc_old_man").empty());
}

TEST_CASE("Engine NPCs: a straight line is walkable only over gentle ground")
{
    // welt #171: route shortcuts went over slopes too steep for the NPC capsule. Searched in the camp's
    // terrain: a stretch steeper than 45 degrees and a flat one.
    Engine engine(scriptConfig());
    REQUIRE(engine.init().ok());
    const auto ground = [&](f32 x, f32 z) -> std::optional<f32>
    {
        const auto hit = engine.physics().raycast(Vec3(x, 200.0f, z), Vec3(0.0f, -1.0f, 0.0f), 400.0f,
                                                  physics::layerBit(physics::Layer::World));
        return hit ? std::optional<f32>(hit->position.y) : std::nullopt;
    };
    std::optional<std::pair<Vec3, Vec3>> steep;
    std::optional<std::pair<Vec3, Vec3>> flat;
    for (f32 angle = 0.0f; angle < 6.28f && (!steep || !flat); angle += 0.2f)
    {
        const Vec3 dir(std::cos(angle), 0.0f, std::sin(angle));
        for (f32 r = 30.0f; r < 150.0f; r += 1.0f)
        {
            const Vec3 p = dir * r;
            const Vec3 q = dir * (r + 2.0f);
            const auto hp = ground(p.x, p.z);
            const auto hq = ground(q.x, q.z);
            const auto hfar = ground(p.x + dir.x * 6.0f, p.z + dir.z * 6.0f);
            if (!hp || !hq || !hfar)
            {
                continue;
            }
            if (!steep && std::abs(*hq - *hp) > 2.2f) // steeper than 47 degrees
            {
                steep = std::pair(Vec3(p.x, *hp, p.z), Vec3(q.x, *hq, q.z));
            }
            if (!flat && std::abs(*hq - *hp) < 0.05f && std::abs(*hfar - *hp) < 0.1f &&
                engine.walkableLine(Vec3(p.x, *hp, p.z), Vec3(p.x + dir.x * 6.0f, *hfar, p.z + dir.z * 6.0f)))
            {
                flat = std::pair(Vec3(p.x, *hp, p.z), Vec3(p.x + dir.x * 6.0f, *hfar, p.z + dir.z * 6.0f));
            }
        }
    }
    REQUIRE_MESSAGE(steep.has_value(), "the camp has no slope steeper than 45 degrees within 150 m");
    REQUIRE(flat.has_value());
    CHECK_FALSE(engine.walkableLine(steep->first, steep->second));
    CHECK_FALSE(engine.walkableLine(steep->second, steep->first)); // down as well
    CHECK(engine.walkableLine(flat->first, flat->second));
    // Long lines are not checked: they go over the waynet.
    CHECK_FALSE(engine.walkableLine(Vec3(-30.0f, 0.0f, 30.0f), Vec3(30.0f, 0.0f, -30.0f)));
}

TEST_CASE("Engine NPCs: stuck on a shortcut, the new way follows the waynet (welt #246)")
{
    // From the camp's south point to the west one the way is a straight shortcut. Held in place (as a capsule
    // wedged at a door jamb), the walker plans again after 1.5 s: along the waynet over the fire, not the
    // same shortcut; let go, it arrives.
    Engine engine(scriptConfig());
    REQUIRE(engine.init().ok());
    run(engine, "teleport(10, 0, 2)"); // near (simulated), off the way
    const auto point = [&](const char* name)
    { return engine.waynet().points()[*engine.waynet().find(name)].position; };
    const Vec3 fire = point("wp_camp_fire");
    REQUIRE(engine.walkableLine(point("wp_camp_south"), point("wp_camp_west")));
    REQUIRE(run(engine, "insert_npc('npc_old_man', 'wp_camp_south')").isString());
    run(engine, "set_routine('npc_old_man', '') npc_clear('npc_old_man')");
    run(engine, "on('npc_arrived', function(npc, target) if npc == 'npc_old_man' then Story.way = 'arrived' "
                "end end)");
    run(engine, "on('npc_blocked', function(npc, target) if npc == 'npc_old_man' then Story.way = 'blocked' "
                "end end)");
    runSeconds(engine, 1.0f);
    run(engine, "npc_goto('npc_old_man', 'wp_camp_west')");
    runSeconds(engine, 0.1f);
    const auto passesFire = [&]
    {
        return run(engine,
                   std::format("for _, q in ipairs(npc_route('npc_old_man')) do if math.abs(q[1] - {}) < 0.3 "
                               "and math.abs(q[3] - {}) < 0.3 then return true end end return false",
                               fire.x, fire.z))
            .asBool();
    };
    CHECK(run(engine, "#npc_route('npc_old_man')").asInteger() == 1); // straight to the west point
    CHECK_FALSE(passesFire());
    run(engine, "npc_debug_hold('npc_old_man', 2.0)");
    runSeconds(engine, 1.8f);
    CHECK(passesFire()); // planned again: over the fire
    for (int i = 0; i < 60 * 40 && run(engine, "Story.way").isNil(); ++i)
    {
        REQUIRE(engine.runFrame());
    }
    CHECK(run(engine, "tostring(Story.way)").asString() == "arrived");
}

TEST_CASE("Engine NPCs: doors - planned through when unlocked, opened on the way, closed behind")
{
    // The test camp's door LAGER_TUER (vob 200): hinge at (30.5, 0, -9), the leaf along +X, closed.
    Engine engine(scriptConfig());
    REQUIRE(engine.init().ok());
    run(engine, "Story.met_gate_guard = true");
    const world::VobId door{200};
    REQUIRE(engine.mobInfo(door).has_value());
    REQUIRE(engine.mobInfo(door)->type == "door");
    CHECK_FALSE(engine.mobInfo(door)->open);
    // A closed but unlocked door is no wall for planning.
    CHECK(engine.walkableLine(Vec3(31.0f, 0.0f, -7.0f), Vec3(31.0f, 0.0f, -11.0f)));
    // welt #246: a point 3 m up (an upper floor) is not reached along the ground below it.
    CHECK_FALSE(engine.walkableLine(Vec3(31.0f, 0.0f, -7.0f), Vec3(31.0f, 3.0f, -11.0f)));

    REQUIRE(run(engine, "insert_npc('npc_old_man', 'wp_camp_guard_bed')").isString());
    run(engine, "set_routine('npc_old_man', '') npc_clear('npc_old_man')");
    run(engine, "teleport(36, 0, -4)");
    run(engine, "npc_goto_point('npc_old_man', 31, 0, -6.5)");
    for (int i = 0;
         i < 60 * 60 && (i < 5 || run(engine, "npc_state('npc_old_man').commands").asInteger() > 0); ++i)
    {
        REQUIRE(engine.runFrame()); // in front of the door
    }
    REQUIRE(run(engine, "npc_state('npc_old_man').z").asNumber() > -8.0);
    run(engine, "on('npc_arrived', function(npc, target) Story.through = target end)");
    run(engine, "npc_goto_point('npc_old_man', 31, 0, -11.5)");
    bool opened = false;
    for (int i = 0; i < 60 * 40 && run(engine, "Story.through").isNil(); ++i)
    {
        REQUIRE(engine.runFrame());
        opened = opened || engine.mobInfo(door)->open;
    }
    CHECK(opened);                                             // he opened it on his way
    CHECK(run(engine, "Story.through").asString() == "point"); // and got through
    CHECK(run(engine, "npc_state('npc_old_man').z").asNumber() < -10.0);
    for (int i = 0; i < 60 * 3; ++i)
    {
        REQUIRE(engine.runFrame());
    }
    CHECK_FALSE(engine.mobInfo(door)->open); // and closed it behind himself

    // Back again: now on the side the leaf swings to (welt #227, going out of an inward door). He waits
    // outside its arc while it opens instead of being pushed aside behind it, and gets through.
    run(engine, "Story.through = nil");
    run(engine, "npc_goto_point('npc_old_man', 31, 0, -6.5)");
    opened = false;
    for (int i = 0; i < 60 * 40 && run(engine, "Story.through").isNil(); ++i)
    {
        REQUIRE(engine.runFrame());
        const auto info = engine.mobInfo(door);
        opened = opened || info->open;
    }
    CHECK(opened);
    CHECK(run(engine, "Story.through").asString() == "point");
    CHECK(run(engine, "npc_state('npc_old_man').z").asNumber() > -7.0);
    for (int i = 0; i < 60 * 3; ++i)
    {
        REQUIRE(engine.runFrame());
    }
    CHECK_FALSE(engine.mobInfo(door)->open); // closed behind himself again
}

TEST_CASE("Engine world: a door open in the world file stands open and is saved closed with open = true")
{
    const char* text =
        R"({"version":1,"name":"d","nextVobId":2,"vobs":[{"id":1,"type":"mob","name":"TUER","rot":[0.0,0.0,0.0,1.0],"mesh":"mobs/door.glb","components":{"mob":{"definition":"door","open":true}}}]})";
    auto file = world::parseWorldFile(text, "d.g7world");
    REQUIRE(file.ok());
    CHECK(file.value().vobs[0].mob.open);
    // Written back the same: "open" after "definition", only when true.
    CHECK(world::writeWorldFile(file.value())
              .find(R"("components":{"mob":{"definition":"door","open":true}})") != std::string::npos);
    file.value().vobs[0].mob.open = false;
    CHECK(world::writeWorldFile(file.value()).find(R"("components":{"mob":{"definition":"door"}})") !=
          std::string::npos);
    CHECK_FALSE(
        world::parseWorldFile(
            R"({"version":1,"name":"d","nextVobId":2,"vobs":[{"id":1,"type":"mob","name":"T","mesh":"m.glb","components":{"mob":{"definition":"door","open":"yes"}}}]})",
            "d")
            .ok());
}
