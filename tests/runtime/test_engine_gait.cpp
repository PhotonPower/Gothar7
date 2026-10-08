// Gait variants (owner decision, figuren #269; label "headless"): an NPC whose figure manifest names an
// `[anim] variant` plays the variant clips of gait.glb where they exist - the old man trots slowly (2.94 m/s,
// the variant clip's own speed) where the woodcutter (a figure without a variant) runs at the base speed.

#include <g7/runtime/Engine.hpp>

#include <doctest/doctest.h>

#include <format>
#include <ostream> // doctest needs it to print std::string operands
#include <string>

using namespace g7;

namespace
{
EngineConfig gaitConfig()
{
    EngineConfig config;
    config.appName = "gait";
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

f32 x(Engine& engine, std::string_view npc)
{
    return static_cast<f32>(run(engine, std::format("npc_state('{}').x", npc)).asNumber());
}
} // namespace

TEST_CASE("Engine gait: the old man trots at his variant's speed, the woodcutter runs at the base speed")
{
    Engine engine(gaitConfig());
    REQUIRE(engine.init().ok());
    run(engine, "teleport(30, 0, 50)"); // the player out of their way
    for (const auto& [npc, z] : {std::pair{"npc_old_man", 30}, std::pair{"npc_woodcutter", 26}})
    {
        REQUIRE(run(engine, std::format("insert_npc('{}', 'wp_camp_center')", npc)).isString());
        run(engine, std::format("set_routine('{}', '') npc_clear('{}') npc_teleport('{}', 40, 0, {}, 90)",
                                npc, npc, npc, z));
    }
    CHECK(run(engine, "npc_state('npc_old_man').gait").asString() == "old");
    CHECK(run(engine, "npc_state('npc_woodcutter').gait").asString().empty()); // no variant: the base clips
    runSeconds(engine, 0.3f);
    run(engine,
        "npc_goto_point('npc_old_man', 75, 0, 30, true) npc_goto_point('npc_woodcutter', 75, 0, 26, true)");
    runSeconds(engine, 1.5f); // up to speed
    const f32 oldFrom = x(engine, "npc_old_man");
    const f32 woodcutterFrom = x(engine, "npc_woodcutter");
    runSeconds(engine, 2.0f);
    const f32 oldSpeed = (x(engine, "npc_old_man") - oldFrom) / 2.0f;
    const f32 woodcutterSpeed = (x(engine, "npc_woodcutter") - woodcutterFrom) / 2.0f;
    MESSAGE("running: old man ", oldSpeed, " m/s, woodcutter ", woodcutterSpeed, " m/s");
    CHECK(oldSpeed == doctest::Approx(2.94f).epsilon(0.1));
    CHECK(woodcutterSpeed == doctest::Approx(4.0f).epsilon(0.1));
}
