// Leonberg's game state (M15 part B; label "headless", own ctest entry; with the generated town, so not in
// CI): the town with its 25 residents after an hour of their routines is captured, put into a fresh engine
// and captured again - the same document; the residents are not inserted twice (world_loaded with restored).

#include <g7/runtime/Engine.hpp>

#include <doctest/doctest.h>

#include <filesystem>
#include <ostream> // doctest needs it to print std::string operands
#include <string>

using namespace g7;

namespace
{
const std::string kGenerated =
    std::string(G7_ASSET_SOURCE_DIR) + "/worlds/leonberg/generated/leonberg_terrain.r16";

EngineConfig leonbergConfig()
{
    EngineConfig config;
    config.appName = "leonberg_save";
    config.headless = true;
    config.world = fs::fromUtf8("worlds/leonberg/leonberg.g7world");
    config.start = "START_MARKTPLATZ";
    config.startTime = "09:00";
    config.fixedFrameSeconds = 1.0 / 60.0;
    return config;
}
} // namespace

TEST_CASE("Leonberg save state: the living town captured, applied to a fresh engine, captured again")
{
    if (!std::filesystem::exists(kGenerated))
    {
        MESSAGE("skipped: Leonberg's generated files are not here");
        return;
    }
    std::string first;
    usize residents = 0;
    {
        Engine engine(leonbergConfig());
        auto started = engine.init();
        REQUIRE_MESSAGE(started.ok(), (started.ok() ? "" : started.error().message));
        for (int i = 0; i < 240; ++i) // four seconds: the residents walk off to their places
        {
            REQUIRE(engine.runFrame());
        }
        auto state = engine.captureGame();
        REQUIRE_MESSAGE(state.ok(), (state.ok() ? "" : state.error().message));
        residents = state.value().worlds.at(0).npcs.size();
        CHECK(residents >= 25);
        first = save::toJson(state.value());
    }
    // The loading engine starts without a world (as from the main menu): Leonberg loads once, from the state.
    EngineConfig empty = leonbergConfig();
    empty.world.clear();
    empty.start.clear();
    Engine engine(std::move(empty));
    REQUIRE(engine.init().ok());
    REQUIRE(engine.runFrame());
    auto state = save::fromJson(first);
    REQUIRE(state.ok());
    REQUIRE(engine.applyGame(state.value()).ok());
    auto again = engine.captureGame();
    REQUIRE(again.ok());
    CHECK(again.value().worlds.at(0).npcs.size() == residents); // nobody inserted twice
    CHECK(save::toJson(again.value()) == first);
    for (int i = 0; i < 30; ++i) // and the town goes on
    {
        REQUIRE(engine.runFrame());
    }
}
