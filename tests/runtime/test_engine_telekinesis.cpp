// Telekinesis in the engine (M17 part T, owner decisions 1-8 of 2026-11-08, label "headless"): held, an item
// lifts and floats into the inventory; let go it falls and lies there (also in a save); out of mana or hit it
// falls; a stranger's item is theft; nothing in reach costs nothing; a scroll is used up when it lifts.

#include <g7/runtime/Engine.hpp>

#include <doctest/doctest.h>

#include <algorithm>
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

EngineConfig config()
{
    EngineConfig c;
    c.appName = "telekinesis";
    c.headless = true;
    c.world = fs::fromUtf8("testworld/camp.g7world");
    c.start = "START_LAGER";
    c.startTime = "12:00";
    c.fixedFrameSeconds = 1.0 / 60.0;
    c.randomSeed = 5;
    return c;
}

void runSeconds(Engine& engine, f32 seconds)
{
    for (int i = 0; i < static_cast<int>(seconds * 60.0f); ++i)
    {
        REQUIRE(engine.runFrame());
    }
}

i64 stat(Engine& engine, std::string_view name)
{
    return run(engine, std::format("stat('{}')", name)).asInteger();
}

/// The hero at the camp looking west with the telekinesis rune drawn and mana to spare.
void ready(Engine& engine, i64 mana = 60)
{
    run(engine, "Story.met_gate_guard = true Story.tk = ''");
    run(engine, "on('telekinesis', function(item, phase) Story.tk = Story.tk .. phase .. ' ' end)");
    run(engine, "on('theft', function(owner, item, count) Story.theft = owner .. ' ' .. item end)");
    run(engine, std::format("give_item('it_rune_telekinesis') equip('it_rune_telekinesis') "
                            "set_talent('magic_circle', 2) set_stat('mana_max', 100) set_stat('mana', {})",
                            mana));
    run(engine, "teleport(41.2, 0, 18.8)");
    runSeconds(engine, 0.3f);
    REQUIRE(run(engine, "draw_magic()").asString() == "it_rune_telekinesis");
    runSeconds(engine, 1.0f);
}

/// An item `metres` ahead of the hero (west) on the ground.
world::VobId itemAhead(Engine& engine, std::string_view instance, f32 metres)
{
    const Vec3 feet = engine.player()->feet();
    auto id = engine.spawnItem(instance, 1, feet + Vec3(-metres, 0.0f, 0.0f));
    REQUIRE(id.ok());
    return id.value();
}

std::optional<WorldItemInfo> lying(Engine& engine, std::string_view instance)
{
    const auto items = engine.worldItems();
    std::optional<WorldItemInfo> found;
    for (const WorldItemInfo& i : items)
    {
        if (i.instance == instance)
        {
            found = i;
        }
    }
    return found;
}
} // namespace

TEST_CASE("Engine telekinesis: held, the item floats into the inventory, costing mana as it goes")
{
    Engine engine(config());
    REQUIRE(engine.init().ok());
    CHECK(engine.scripts()->errors().empty());
    const auto spell = engine.spellOfItem("it_rune_telekinesis");
    REQUIRE(spell.has_value());
    CHECK(spell->kind == gameplay::SpellKind::Telekinesis);
    CHECK(spell->circle == 2);
    CHECK(spell->manaPerSecond == doctest::Approx(2.0f));
    CHECK(spell->blockedDrop == std::optional<f32>(1.0f)); // owner decision 2026-11-08: stuck 1 s, it falls
    CHECK(engine.spellOfItem("it_scroll_telekinesis").has_value());
    CHECK(run(engine, "telekinesis_allowed('it_apple')").asBool());

    ready(engine);
    const i64 apples = run(engine, "item_count('it_apple')").asInteger();
    const world::VobId apple = itemAhead(engine, "it_apple", 8.0f);
    run(engine, "hero_cast(true)");
    runSeconds(engine, 1.0f);
    REQUIRE_FALSE(run(engine, "hero_telekinesis()").isNil());
    CHECK(run(engine, "hero_telekinesis().item").asString() == "it_apple");
    CHECK(run(engine, "hero_telekinesis().phase").asString() == "float");
    CHECK(stat(engine, "mana") <= 60 - 10 - 1); // decision 1: 10 when it lifts, 2 a second
    CHECK(stat(engine, "mana") >= 60 - 10 - 2);
    // From 8 m at 3 m/s it arrives within about 3 s; the hero stands meanwhile.
    for (int i = 0; i < 6 * 60 && !run(engine, "hero_telekinesis()").isNil(); ++i)
    {
        REQUIRE(engine.runFrame());
    }
    CHECK(run(engine, "hero_telekinesis()").isNil());
    CHECK(run(engine, "item_count('it_apple')").asInteger() == apples + 1);
    const auto items = engine.worldItems();
    CHECK(std::none_of(items.begin(), items.end(), [&](const WorldItemInfo& i) { return i.vob == apple; }));
    CHECK(run(engine, "Story.tk").asString() == "lift float arrive ");
    CHECK(stat(engine, "mana") >= 60 - 10 - 2 * 5);
    CHECK(run(engine, "Story.theft").isNil()); // a nobody's apple
    run(engine, "hero_cast(false)");
}

TEST_CASE(
    "Engine telekinesis: let go it falls and lies there, also in a save; a save while it floats lands it")
{
    Engine engine(config());
    REQUIRE(engine.init().ok());
    ready(engine);
    const Vec3 feet = engine.player()->feet();
    itemAhead(engine, "it_bread", 10.0f);
    run(engine, "hero_cast(true)");
    runSeconds(engine, 1.5f);
    run(engine, "hero_cast(false)");
    runSeconds(engine, 2.0f);
    CHECK(run(engine, "hero_telekinesis()").isNil());
    CHECK(run(engine, "Story.tk").asString() == "lift float drop ");
    const auto bread = lying(engine, "it_bread");
    REQUIRE(bread.has_value());
    CHECK(bread->position.x > feet.x - 9.5f);                          // came closer before it fell
    CHECK(bread->position.y == doctest::Approx(feet.y).epsilon(0.05)); // on the ground again
    auto state = engine.captureGame();
    REQUIRE_MESSAGE(state.ok(), (state.ok() ? "" : state.error().message));
    const auto& runtime = state.value().worlds.back().delta.runtime;
    CHECK(std::any_of(runtime.begin(), runtime.end(),
                      [&](const world::WorldFileVob& v)
                      {
                          return v.item.instance == "it_bread" &&
                                 std::abs(v.transform.position.x - bread->position.x) < 0.01f;
                      }));

    // Saving while it floats: it lands where it is first.
    itemAhead(engine, "it_apple", 10.0f);
    run(engine, "hero_cast(true)");
    runSeconds(engine, 1.5f);
    REQUIRE_FALSE(run(engine, "hero_telekinesis()").isNil());
    REQUIRE(engine.captureGame().ok());
    CHECK(run(engine, "hero_telekinesis()").isNil());
    const auto apple = lying(engine, "it_apple");
    REQUIRE(apple.has_value());
    CHECK(apple->position.y == doctest::Approx(feet.y).epsilon(0.05));
    run(engine, "hero_cast(false)");
}

TEST_CASE(
    "Engine telekinesis: nothing in reach costs nothing; out of mana or hit it falls; a scroll is used up")
{
    Engine engine(config());
    REQUIRE(engine.init().ok());
    ready(engine, 60);
    // Nothing within 15 m ahead: a notice, no mana spent.
    run(engine, "hero_cast(true)");
    runSeconds(engine, 0.5f);
    run(engine, "hero_cast(false)");
    runSeconds(engine, 0.5f);
    CHECK(run(engine, "Story.tk").asString().empty());
    CHECK(stat(engine, "mana") == 60);
    itemAhead(engine, "it_apple", 20.0f); // out of reach (decision 2: 15 m)
    run(engine, "hero_cast(true)");
    runSeconds(engine, 0.5f);
    run(engine, "hero_cast(false)");
    runSeconds(engine, 0.5f);
    CHECK(run(engine, "Story.tk").asString().empty());
    CHECK(stat(engine, "mana") == 60);

    // Out of mana: 10 to lift, one second more, then it falls.
    run(engine, "set_stat('mana', 12)");
    itemAhead(engine, "it_apple", 12.0f);
    run(engine, "hero_cast(true)");
    runSeconds(engine, 2.5f);
    CHECK(run(engine, "hero_telekinesis()").isNil());
    CHECK(run(engine, "Story.tk").asString() == "lift float drop ");
    CHECK(stat(engine, "mana") < 2);
    run(engine, "hero_cast(false)");
    runSeconds(engine, 1.0f);

    // A hit lets it fall (the apple from before, nearer now).
    run(engine, "set_stat('mana', 60) Story.tk = ''");
    run(engine, "hero_cast(true)");
    runSeconds(engine, 0.8f);
    REQUIRE_FALSE(run(engine, "hero_telekinesis()").isNil());
    run(engine, "set_stat('hp', stat('hp') - 5)");
    runSeconds(engine, 0.2f);
    CHECK(run(engine, "Story.tk").asString() == "lift float drop ");
    run(engine, "hero_cast(false)");
    runSeconds(engine, 1.0f);
    // Fetched again from where it fell.
    run(engine, "Story.tk = '' hero_cast(true)");
    runSeconds(engine, 5.0f);
    run(engine, "hero_cast(false)");
    runSeconds(engine, 0.5f);
    CHECK(run(engine, "Story.tk").asString() == "lift float arrive ");

    // A stranger's item is theft (decision 5); from a scroll, the scroll goes when it lifts.
    run(engine, "give_item('it_scroll_telekinesis') equip('it_scroll_telekinesis') hero_rune(2)");
    runSeconds(engine, 1.0f);
    REQUIRE(run(engine, "player_weapon()").asString() == "magic");
    const world::VobId bread = itemAhead(engine, "it_bread", 4.0f);
    engine.scene().get<world::ItemRef>(engine.scene().findById(bread))->owner = "guard";
    run(engine, "hero_cast(true)");
    runSeconds(engine, 0.3f);
    CHECK(run(engine, "item_count('it_scroll_telekinesis')").asInteger() == 0);
    runSeconds(engine, 3.0f);
    CHECK(run(engine, "Story.theft").asString() == "guard it_bread");
    run(engine, "hero_cast(false)");
}
