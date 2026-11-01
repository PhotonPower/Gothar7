// Leonberg's doors (label "headless", with the generated town; not in CI): an NPC goes out through a door
// that opens inwards. Its own ctest entry (runtime_leonberg_doors) for its run time.

#include <g7/runtime/Engine.hpp>

#include <doctest/doctest.h>

#include <filesystem>
#include <format>
#include <ostream> // doctest needs it to print std::string operands
#include <string>

using namespace g7;

namespace
{
const std::string kGenerated =
    std::string(G7_ASSET_SOURCE_DIR) + "/worlds/leonberg/generated/leonberg_terrain.r16";

script::Value run(Engine& engine, std::string_view line)
{
    auto result = engine.runConsoleLine(line);
    const std::string what = std::string(line) + ": " + (result.ok() ? "" : result.error().message);
    REQUIRE_MESSAGE(result.ok(), what);
    return result.value();
}
} // namespace

TEST_CASE(
    "Leonberg: out through an inward door - waiting outside the leaf's arc, not pushed beside the hinge "
    "(with the generated town)")
{
    // welt #227: house Zl2-T2, its door opens into the house. In, the leaf swings away; out, towards the NPC.
    if (!std::filesystem::exists(kGenerated))
    {
        MESSAGE("skipped: Leonberg's generated files are not here");
        return;
    }
    EngineConfig config;
    config.appName = "leonberg";
    config.headless = true;
    config.world = fs::fromUtf8("worlds/leonberg/leonberg.g7world");
    config.start = "START_MARKTPLATZ";
    config.fixedFrameSeconds = 1.0 / 60.0;
    Engine engine(std::move(config));
    REQUIRE(engine.init().ok());
    run(engine, "teleport(-18.3, 0, 18.5)"); // near by: simulated
    run(engine, "on('npc_arrived', function(npc, target) if npc == 'npc_leo_citizen' then Story.at = target "
                "end end)");
    run(engine, "on('npc_blocked', function(npc, target) if npc == 'npc_leo_citizen' then Story.at = "
                "'blocked' end end)");
    REQUIRE(run(engine, "insert_npc('npc_leo_citizen', 'WP_LEO_WOHNHAUS_ZL2_T2')").isString());
    run(engine, "set_routine('npc_leo_citizen', '') npc_clear('npc_leo_citizen')");
    const auto walk = [&](std::string_view goal)
    {
        run(engine, "Story.at = nil");
        run(engine, std::format("npc_goto('npc_leo_citizen', '{}')", goal));
        for (int i = 0; i < 60 * 40 && run(engine, "Story.at").isNil(); ++i)
        {
            REQUIRE(engine.runFrame());
        }
        return std::string(run(engine, "tostring(Story.at)").asString());
    };
    CHECK(walk("WP_LEO_WOHNHAUS_ZL2_T2_KAMMER") == "WP_LEO_WOHNHAUS_ZL2_T2_KAMMER");
    CHECK(walk("WP_LEO_WOHNHAUS_ZL2_T2") == "WP_LEO_WOHNHAUS_ZL2_T2");
}
