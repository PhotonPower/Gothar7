// The game state in the engine (M15 part B, docs/modules/save.md): the camp changed in many ways is captured,
// put into a fresh engine and captured again - the same document. NPCs come back with their values, a dead
// wolf stays dead, the dropped item lies where it lay, the chest keeps what was taken out, Story and named
// timers return; the world's scripts hear world_loaded(world, true).

#include <g7/runtime/Engine.hpp>

#include <doctest/doctest.h>

#include <format>
#include <ostream> // doctest needs it to print std::string operands
#include <string>

using namespace g7;

namespace
{
EngineConfig campConfig()
{
    EngineConfig config;
    config.appName = "save";
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
    REQUIRE_MESSAGE(result.ok(), std::string(line), ": ", (result.ok() ? "" : result.error().message));
    return result.value();
}

void frames(Engine& engine, int n)
{
    for (int i = 0; i < n; ++i)
    {
        REQUIRE(engine.runFrame());
    }
}

std::string captured(Engine& engine)
{
    auto state = engine.captureGame();
    REQUIRE_MESSAGE(state.ok(), (state.ok() ? "" : state.error().message));
    return save::toJson(state.value());
}

/// Diagnostics: the first line where two documents differ.
std::string firstDifference(const std::string& a, const std::string& b)
{
    usize line = 1;
    usize start = 0;
    for (usize i = 0; i < std::min(a.size(), b.size()); ++i)
    {
        if (a[i] != b[i])
        {
            return std::format("line {}: '{}' vs '{}'", line, a.substr(start, a.find('\n', i) - start),
                               b.substr(start, b.find('\n', i) - start));
        }
        if (a[i] == '\n')
        {
            ++line;
            start = i + 1;
        }
    }
    return a.size() == b.size() ? std::string() : "length differs";
}
} // namespace

TEST_CASE("Save state: the changed camp captured, applied to a fresh engine, captured again - the same")
{
    std::string first;
    {
        Engine engine(campConfig());
        REQUIRE(engine.init().ok());
        frames(engine, 3);
        run(engine, "insert_npc('npc_gate_guard') insert_npc('npc_old_man') insert_npc('mon_wolf', "
                    "'wp_camp_center')");
        frames(engine, 30);
        run(engine, "npc_set_stat('npc_gate_guard', 'hp', 17)");
        run(engine, "npc_give_item('npc_old_man', 'it_beer', 2)");
        const auto wolf = engine.npcByInstance("mon_wolf");
        REQUIRE(wolf.has_value());
        REQUIRE(engine.creatureAction(*wolf, "die"));
        run(engine, "give_item('it_apple', 3) give_item('it_sword_old') equip('it_sword_old')");
        run(engine, "set_stat('str', 25) set_talent('melee_1h', 1) add_xp(120)");
        run(engine, "drop_item('it_apple', 1)");
        const auto chest = engine.findMob("LAGER_TRUHE");
        REQUIRE(chest.has_value());
        const auto contents = engine.mobInfo(*chest)->contents;
        REQUIRE_FALSE(contents.empty());
        REQUIRE(engine.takeFromMob(*chest, contents.front().item, contents.front().count).ok());
        run(engine, "unlock('LAGER_TUER_ZU')");
        run(engine, "Story.test = { count = 3, name = 'Ring', list = { 1, 2.5, 'x' }, flag = true }");
        run(engine, "after(600, 'camp_people') every(900, 'print', 'tick', 2)");
        run(engine, "time(15, 30) teleport(10, 0, -3)");
        frames(engine, 20);
        first = captured(engine);
        // A function in Story cannot be saved: the capture says so.
        run(engine, "Story.bad = function() end");
        auto refused = engine.captureGame();
        CHECK_FALSE(refused.ok());
        run(engine, "Story.bad = nil");
        CHECK(captured(engine) == first);
    }

    Engine engine(campConfig());
    REQUIRE(engine.init().ok());
    frames(engine, 3);
    run(engine, "Story.heard = {} on('world_loaded', function(w, restored) Story.heard[#Story.heard + 1] = "
                "restored end)");
    auto state = save::fromJson(first);
    REQUIRE_MESSAGE(state.ok(), (state.ok() ? "" : state.error().message));
    // What was captured: three NPCs (the wolf dead), the dropped apple, the mobs, two named timers.
    REQUIRE(state.value().worlds.size() == 1);
    const save::WorldState& camp = state.value().worlds[0];
    REQUIRE(camp.npcs.size() == 3);
    CHECK(camp.npcs[2].life == save::Life::Dead);
    REQUIRE(camp.delta.runtime.size() == 1);
    CHECK(camp.delta.runtime[0].item.instance == "it_apple");
    CHECK(camp.mobs.size() >= 4);
    CHECK(state.value().timers.size() == 2);
    REQUIRE(engine.applyGame(state.value()).ok());
    const std::string second = captured(engine);
    CHECK_MESSAGE(second == first, firstDifference(first, second));

    // The details, as the game sees them.
    CHECK(run(engine, "npc_stat('npc_gate_guard', 'hp')").asInteger() == 17);
    CHECK(run(engine, "npc_item_count('npc_old_man', 'it_beer')").asInteger() >= 2);
    CHECK(run(engine, "fight_state('mon_wolf')").asString() != "ready");
    CHECK(run(engine, "item_count('it_apple')").asInteger() >= 2);
    CHECK(run(engine, "equipped('melee')").asString() == "it_sword_old");
    CHECK(run(engine, "stat('str')").asInteger() == 25);
    CHECK(run(engine, "Story.test.list[2]").asNumber() == doctest::Approx(2.5));
    CHECK(run(engine, "Story.test.name").asString() == "Ring");
    CHECK(run(engine, "where().time").asString().starts_with("15:30"));
    CHECK(engine.mobInfo(*engine.findMob("LAGER_TUER_ZU"))->locked == false);
    CHECK(std::ranges::none_of(engine.mobInfo(*engine.findMob("LAGER_TRUHE"))->contents,
                               [](const auto& s) { return s.count == 0; }));
    CHECK(std::ranges::any_of(engine.worldItems(),
                              [](const WorldItemInfo& i) { return i.instance == "it_apple"; }));
    // Story is the saved one: the handler of this engine ran with restored = true, then Story was replaced.
    CHECK(run(engine, "Story.heard == nil").asBool());
}

TEST_CASE("Save state: visited worlds keep their state over level changes and in the game state (part C)")
{
    Engine engine(campConfig());
    REQUIRE(engine.init().ok());
    frames(engine, 3);
    run(engine, "insert_npc('npc_old_man') insert_npc('mon_wolf', 'wp_camp_center')");
    frames(engine, 10);
    run(engine, "npc_set_stat('npc_old_man', 'hp', 23)");
    const auto wolf = engine.npcByInstance("mon_wolf");
    REQUIRE(wolf.has_value());
    REQUIRE(engine.creatureAction(*wolf, "die"));
    run(engine, "give_item('it_apple', 2) drop_item('it_apple', 1)");
    const auto chest = engine.findMob("LAGER_TRUHE");
    REQUIRE(chest.has_value());
    const auto contents = engine.mobInfo(*chest)->contents;
    REQUIRE(engine.takeFromMob(*chest, contents.front().item, contents.front().count).ok());
    const usize chestStacks = engine.mobInfo(*chest)->contents.size();
    const auto apples = [&]
    {
        return std::ranges::count_if(engine.worldItems(), [](const WorldItemInfo& i)
                                     { return i.instance == "it_apple" && i.vob.runtime(); });
    };
    REQUIRE(apples() == 1);

    // Into the cave and back: the camp is as it was left - nobody inserted again, nobody revived.
    run(engine, "Story.heard = {} on('world_loaded', function(w, restored) Story.heard[#Story.heard + 1] = "
                "restored end)");
    engine.requestWorldChange("testworld/cave.g7world", "START_HOEHLE");
    frames(engine, 2);
    REQUIRE(engine.worldPath() == "testworld/cave.g7world");
    CHECK(engine.keptWorlds() == 1);
    CHECK(engine.creatureCount() == 0);
    engine.requestWorldChange("testworld/camp.g7world", "START_LAGER_HOEHLE");
    frames(engine, 2);
    REQUIRE(engine.worldPath() == "testworld/camp.g7world");
    CHECK(engine.keptWorlds() == 1); // now the cave
    CHECK(run(engine, "Story.heard[1] == false and Story.heard[2] == true").asBool());
    CHECK(engine.creatureCount() == 2);
    CHECK(run(engine, "npc_stat('npc_old_man', 'hp')").asInteger() == 23);
    CHECK(run(engine, "fight_state('mon_wolf')").asString() != "ready");
    CHECK(apples() == 1);
    CHECK(engine.mobInfo(*engine.findMob("LAGER_TRUHE"))->contents.size() == chestStacks);

    // The game state holds both worlds; a fresh engine gets both back and the same document.
    auto state = engine.captureGame();
    REQUIRE(state.ok());
    REQUIRE(state.value().worlds.size() == 2);
    CHECK(state.value().worlds[0].path == "testworld/cave.g7world");
    CHECK(state.value().worlds[1].path == "testworld/camp.g7world");
    const std::string first = save::toJson(state.value());
    Engine loaded(campConfig());
    REQUIRE(loaded.init().ok());
    frames(loaded, 3);
    REQUIRE(loaded.applyGame(state.value()).ok());
    CHECK(loaded.keptWorlds() == 1);
    const std::string second = captured(loaded);
    CHECK_MESSAGE(second == first, firstDifference(first, second));
    // The loaded game goes to the cave and back: still the same camp.
    loaded.requestWorldChange("testworld/cave.g7world", "START_HOEHLE");
    frames(loaded, 2);
    loaded.requestWorldChange("testworld/camp.g7world", "START_LAGER_HOEHLE");
    frames(loaded, 2);
    CHECK(run(loaded, "npc_stat('npc_old_man', 'hp')").asInteger() == 23);
    CHECK(loaded.creatureCount() == 2);
}
