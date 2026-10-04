// Scripts in the engine (M7 part C, headless): game/scripts load with the session, the console runs Lua,
// insert/goto/time/where act on the world, world_loaded and timers reach the scripts, a reload keeps Story.

#include <g7/physics/Character.hpp>
#include <g7/runtime/Engine.hpp>

#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
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

    // A dialog through the console helper; Story remembers it.
    CHECK(run(engine, "call_info('dia_gate_guard_hello')").asBool());
    CHECK(printed(engine, "Torwache: Halt! Wer bist du?"));
    CHECK_FALSE(run(engine, "call_info('dia_gate_guard_hello')").asBool());
    CHECK(run(engine, "call_info('dia_gate_guard_work')").asBool());
    CHECK(run(engine, "Story.quest_farm_work").asString() == "running");

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
    CHECK(lying == 3);
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
    CHECK(info.contents.size() == 2);

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
    CHECK(engine.mobInfo(*chest)->contents.size() == 2);
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
