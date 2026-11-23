// Spell glow in the engine (M17 part G, decisions 9 and 10, label "headless"): the hero's hand glows in the
// spell's colour while he casts; a fire bolt's hit flashes the target orange for 0.3 s; none afterwards.

#include <g7/runtime/Engine.hpp>

#include <doctest/doctest.h>

#include <format>
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

void runSeconds(Engine& engine, f32 seconds)
{
    for (int i = 0; i < static_cast<int>(seconds * 60.0f); ++i)
    {
        REQUIRE(engine.runFrame());
    }
}
} // namespace

TEST_CASE("Engine spell glow: the hand glows while casting, a hit flashes 0.3 s in the damage's colour")
{
    EngineConfig config;
    config.appName = "glow";
    config.headless = true;
    config.world = fs::fromUtf8("testworld/camp.g7world");
    config.start = "START_LAGER";
    config.startTime = "12:00";
    config.fixedFrameSeconds = 1.0 / 60.0;
    config.randomSeed = 5;
    Engine engine(std::move(config));
    REQUIRE(engine.init().ok());
    REQUIRE(engine.spellFx().spells.contains("spl_firebolt"));
    CHECK_FALSE(engine.figureGlow("hero").active());

    run(engine, "Story.met_gate_guard = true give_item('it_rune_firebolt') equip('it_rune_firebolt') "
                "set_talent('magic_circle', 1) set_stat('mana_max', 50) set_stat('mana', 50)");
    run(engine, "teleport(41.2, 0, 18.8)");
    REQUIRE(run(engine, "insert_npc('npc_old_man', 'wp_camp_center')").isString());
    run(engine, "set_routine('npc_old_man', '') npc_clear('npc_old_man') "
                "npc_teleport('npc_old_man', 33.2, 0, 18.8, 270)");
    runSeconds(engine, 0.3f);
    REQUIRE(run(engine, "draw_magic()").asString() == "it_rune_firebolt");
    runSeconds(engine, 1.0f);
    CHECK_FALSE(engine.figureGlow("npc_old_man").active());

    run(engine, "hero_cast(true)");
    runSeconds(engine, 0.1f);
    run(engine, "hero_cast(false)");
    runSeconds(engine, 0.05f);
    const render::FigureGlow casting = engine.figureGlow("hero");
    CHECK(casting.hand.r > casting.hand.b);      // decision 9: orange for the fire bolt
    CHECK(casting.arm == render::GlowArm::Left); // the bolt clip pushes with the left hand
    CHECK(casting.hand.r > 1.0f);
    CHECK(casting.rim.r < casting.hand.r); // the outline only faintly
    CHECK(casting.fill.r == 0.0f);

    // The bolt flies 8 m at 30 m/s; the old man flashes when it hits, for about 0.3 s.
    i32 flashFrames = 0;
    f32 strongest = 0.0f;
    for (int i = 0; i < 3 * 60; ++i)
    {
        REQUIRE(engine.runFrame());
        const render::FigureGlow g = engine.figureGlow("npc_old_man");
        if (g.fill.r > 0.0f)
        {
            ++flashFrames;
            strongest = std::max(strongest, g.fill.r);
            CHECK(g.fill.r > g.fill.b); // orange: fire
        }
    }
    CHECK(flashFrames == doctest::Approx(18).epsilon(0.15)); // 0.3 s at 60 Hz
    CHECK(strongest > 0.2f);
    CHECK_FALSE(engine.figureGlow("npc_old_man").active());
    CHECK_FALSE(engine.figureGlow("hero").active()); // the cast is over
}
