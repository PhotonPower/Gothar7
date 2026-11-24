// Dungeon doors and niches in the engine (docs/modules/dungeon.md D5) on testworld/doors.g7world: a door
// that only its key opens (no_pick), a one-way door, and a niche that opens like a chest.

#include <g7/runtime/Engine.hpp>

#include <doctest/doctest.h>

#include <format>
#include <ostream> // doctest needs it to print std::string operands
#include <string>

using namespace g7;

namespace
{
EngineConfig doorsConfig()
{
    EngineConfig config;
    config.appName = "doors";
    config.headless = true;
    config.world = fs::fromUtf8("testworld/doors.g7world");
    config.start = "START_TUEREN";
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

/// Uses a mob; true if the hero started using it. Waits until he is done (or the container is open).
bool use(Engine& engine, std::string_view name)
{
    const auto mob = engine.findMob(name);
    REQUIRE(mob.has_value());
    if (auto used = engine.useMob(*mob); !used)
    {
        MESSAGE(std::string(name), ": ", used.error().message);
        return false;
    }
    for (int i = 0; i < 600 && engine.mobPhase().has_value() && engine.mobPhase() != "loop"; ++i)
    {
        REQUIRE(engine.runFrame());
    }
    frames(engine, 2);
    return true;
}

MobInfo info(const Engine& engine, std::string_view name)
{
    const auto mob = engine.findMob(name);
    REQUIRE(mob.has_value());
    return *engine.mobInfo(*mob);
}
} // namespace

TEST_CASE(
    "Engine doors: no_pick needs the key, one_way opens from its front only, a niche opens like a chest")
{
    Engine engine(doorsConfig());
    auto result = engine.init();
    REQUIRE_MESSAGE(result.ok(), (result.ok() ? "" : result.error().message));
    frames(engine, 3);

    // The lockpick does not help.
    run(engine, "give_item('it_lockpick')");
    run(engine, "teleport(4.5, 0, 1.5)");
    frames(engine, 2);
    CHECK(info(engine, "TUER_SCHLUESSEL").locked);
    CHECK_FALSE(use(engine, "TUER_SCHLUESSEL"));
    CHECK(info(engine, "TUER_SCHLUESSEL").locked);
    run(engine, "give_item('it_key_hut_door')");
    CHECK(use(engine, "TUER_SCHLUESSEL"));
    CHECK_FALSE(info(engine, "TUER_SCHLUESSEL").locked);
    CHECK(info(engine, "TUER_SCHLUESSEL").open);

    // From behind the one-way door will not open; from the front it does, and closes from either side.
    run(engine, "teleport(-3.5, 0, -1.5)");
    frames(engine, 2);
    CHECK_FALSE(use(engine, "TUER_EINWEG"));
    CHECK_FALSE(info(engine, "TUER_EINWEG").open);
    run(engine, "teleport(-3.5, 0, 1.5)");
    frames(engine, 2);
    CHECK(use(engine, "TUER_EINWEG"));
    CHECK(info(engine, "TUER_EINWEG").open);
    run(engine, "teleport(-3.5, 0, -1.5)");
    frames(engine, 2);
    CHECK(use(engine, "TUER_EINWEG"));
    CHECK_FALSE(info(engine, "TUER_EINWEG").open);

    // The niche: the hero reaches in, its contents open as with a chest.
    run(engine, "teleport(0, 0, -2.5)");
    frames(engine, 2);
    CHECK(use(engine, "NISCHE"));
    CHECK(engine.inventoryOpen());
    CHECK(engine.mobPhase() == std::optional<std::string_view>("loop"));
}
