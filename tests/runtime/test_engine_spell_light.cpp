// The light spell (owner's wish 2026-11-26): an orb of plasma over the hero's left hand with a point light;
// over the shoulder while that hand is busy; it goes out after its time, casting it again renews it, a save
// keeps the time it has left.

#include <g7/runtime/Engine.hpp>
#include <g7/save/SaveFile.hpp>

#include <doctest/doctest.h>

#include <algorithm>
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

EngineConfig lightConfig()
{
    EngineConfig config;
    config.appName = "light";
    config.headless = true;
    config.world = fs::fromUtf8("testworld/camp.g7world");
    config.start = "START_LAGER";
    config.startTime = "12:00";
    config.fixedFrameSeconds = 1.0 / 60.0;
    config.randomSeed = 3;
    return config;
}

void runSeconds(Engine& engine, f32 seconds)
{
    for (int i = 0; i < static_cast<int>(seconds * 60.0f); ++i)
    {
        REQUIRE(engine.runFrame());
    }
}

/// Draws the rune or scroll `item`, casts it, waits for the clip and puts the magic away again.
void castLight(Engine& engine, std::string_view item)
{
    REQUIRE(run(engine, "draw_magic()").asString() == std::string(item));
    runSeconds(engine, 1.0f);
    run(engine, "hero_cast(true)");
    runSeconds(engine, 0.1f);
    run(engine, "hero_cast(false)");
    runSeconds(engine, 1.5f);
}

Vec3 overHand(const Engine& engine)
{
    const auto hand = engine.playerSocketTransform("socket_hand_l");
    REQUIRE(hand.has_value());
    return Vec3((*hand)[3]) + Vec3(0.0f, 0.15f, 0.0f);
}

Vec3 overShoulder(const Engine& engine)
{
    const auto shoulder = engine.playerSocketTransform("upperarm_l");
    REQUIRE(shoulder.has_value());
    return Vec3((*shoulder)[3]) + Vec3(0.0f, 0.25f, 0.0f);
}
} // namespace

TEST_CASE(
    "Engine light spell: the orb over the hand, over the shoulder while the hand is busy, and its light")
{
    Engine engine(lightConfig());
    REQUIRE(engine.init().ok());
    CHECK(engine.scripts()->errors().empty());
    const auto spell = engine.spellOfItem("it_rune_light");
    REQUIRE(spell.has_value());
    CHECK(spell->kind == gameplay::SpellKind::Light);
    CHECK(spell->circle == 1);
    CHECK(spell->mana == 10);
    CHECK(spell->duration == doctest::Approx(300.0f));
    CHECK(engine.spellOfItem("it_scroll_light").has_value());
    CHECK_FALSE(engine.lightOrbLight().has_value());

    run(engine, "give_item('it_rune_light') equip('it_rune_light') set_talent('magic_circle', 1) "
                "set_stat('mana_max', 30) set_stat('mana', 30)");
    run(engine, "Story.lights = 0 on('light_on', function(spell, s) Story.lights = Story.lights + 1 end)");
    castLight(engine, "it_rune_light");
    CHECK(run(engine, "stat('mana')").asInteger() == 20);
    CHECK(run(engine, "Story.lights").asInteger() == 1);
    REQUIRE(engine.lightSecondsLeft() > 290.0f);
    CHECK(run(engine, "hero_light_seconds()").asNumber() == doctest::Approx(engine.lightSecondsLeft()));
    // The rune is still in the left hand: the orb waits over the shoulder.
    CHECK(engine.lightOrbAtShoulder());
    CHECK(glm::length(*engine.lightOrbPosition() - overShoulder(engine)) < 0.05f);

    // The magic put away: back over the open hand, wobbling only a little.
    CHECK(run(engine, "draw_magic()").asString() == "none");
    runSeconds(engine, 1.5f);
    CHECK_FALSE(engine.lightOrbAtShoulder());
    CHECK(glm::length(*engine.lightOrbPosition() - overHand(engine)) < 0.05f);
    // It follows the hand: the hero walks on, the orb with him.
    run(engine, "teleport(30, 0, 18)");
    runSeconds(engine, 0.5f);
    CHECK(glm::length(*engine.lightOrbPosition() - overHand(engine)) < 0.05f);

    // Its light: about 8 m, bright, where the orb is.
    const auto light = engine.lightOrbLight();
    REQUIRE(light.has_value());
    CHECK(light->radius >= 8.0f);
    CHECK(light->intensity > 2.0f);
    CHECK(glm::length(light->position - *engine.lightOrbPosition()) < 1e-4f);

    // A two-handed weapon drawn: over the shoulder; put away: back over the hand.
    run(engine, "set_stat('str', 60) give_item('it_sword_2h') equip('it_sword_2h')");
    CHECK(run(engine, "draw_weapon()").asString() != "none");
    runSeconds(engine, 1.5f);
    CHECK(engine.lightOrbAtShoulder());
    CHECK(glm::length(*engine.lightOrbPosition() - overShoulder(engine)) < 0.05f);
    run(engine, "draw_weapon()");
    runSeconds(engine, 1.5f);
    CHECK_FALSE(engine.lightOrbAtShoulder());

    // The torch takes the left hand too.
    run(engine, "give_item('it_torch')");
    CHECK(run(engine, "hero_torch()").asBool());
    runSeconds(engine, 2.0f);
    CHECK(engine.lightOrbAtShoulder());
    CHECK_FALSE(run(engine, "hero_torch()").asBool());
    runSeconds(engine, 1.0f);
    CHECK_FALSE(engine.lightOrbAtShoulder());
}

TEST_CASE("Engine light spell: it goes out after its time, flickering and fading; cast again it is renewed")
{
    Engine engine(lightConfig());
    REQUIRE(engine.init().ok());
    // A short light for the test (the real one shines 300 s), from a scroll: no circle needed.
    run(engine,
        "Spell 'spl_test_light' { name = 'Test', circle = 1, mana = 5, kind = 'light', duration = 12, "
        "light = { range = 8, intensity = 3, flicker = 0.05, warn = 10, warn_flicker = 0.6, fade = 1 }, "
        "fx = { orb = 'light_orb', sparks = 'light_sparks' } } "
        "Item 'it_scroll_test_light' { name = 'Test', category = 'scroll', spell = 'spl_test_light', "
        "value = 1 }");
    run(engine, "give_item('it_scroll_test_light', 2) equip('it_scroll_test_light') set_stat('mana_max', 30) "
                "set_stat('mana', 30)");
    run(engine, "Story.out = 0 on('light_out', function(spell) Story.out = Story.out + 1 end)");
    castLight(engine, "it_scroll_test_light");
    CHECK(run(engine, "draw_magic()").asString() == "none");
    REQUIRE(engine.lightOrbLight().has_value());
    const f32 left = engine.lightSecondsLeft();
    CHECK(left < 12.0f);
    CHECK(left > 8.0f);

    // In the last seconds it flickers more strongly: the light's swing over a second is larger.
    const auto swing = [&]
    {
        f32 low = 1e9f;
        f32 high = 0.0f;
        for (int i = 0; i < 60; ++i)
        {
            REQUIRE(engine.runFrame());
            const f32 intensity = engine.lightOrbLight()->intensity;
            low = std::min(low, intensity);
            high = std::max(high, intensity);
        }
        return high - low;
    };
    const f32 early = swing(); // about 10 s left: the warning has barely begun
    runSeconds(engine, engine.lightSecondsLeft() - 2.0f);
    const f32 late = swing(); // the last 2 s
    CHECK(late > early * 3.0f);

    // Out: a fade of a second, then nothing; the event once.
    runSeconds(engine, 1.0f + 0.1f);
    REQUIRE(engine.lightOrbLight().has_value()); // fading
    CHECK(engine.lightSecondsLeft() == doctest::Approx(0.0f));
    CHECK(engine.lightOrbLight()->intensity < 3.0f * 0.95f);
    runSeconds(engine, 1.1f);
    CHECK_FALSE(engine.lightOrbLight().has_value());
    CHECK_FALSE(engine.lightOrbPosition().has_value());
    CHECK(run(engine, "Story.out").asInteger() == 1);

    // Cast again: a new light; cast once more while it shines: renewed to its full time, not a second orb.
    castLight(engine, "it_scroll_test_light");
    CHECK(run(engine, "draw_magic()").asString() == "none");
    REQUIRE(engine.lightOrbLight().has_value());
    runSeconds(engine, 4.0f);
    const f32 before = engine.lightSecondsLeft();
    CHECK(before < 8.0f);
    run(engine, "give_item('it_scroll_test_light') equip('it_scroll_test_light')");
    castLight(engine, "it_scroll_test_light");
    CHECK(engine.lightSecondsLeft() > before + 2.5f);
    CHECK(engine.lightSecondsLeft() < 12.0f);
}

TEST_CASE("Engine light spell: a save keeps the time it has left")
{
    std::string json;
    f32 left = 0.0f;
    {
        Engine engine(lightConfig());
        REQUIRE(engine.init().ok());
        run(engine, "give_item('it_scroll_light') equip('it_scroll_light') set_stat('mana_max', 30) "
                    "set_stat('mana', 30)");
        castLight(engine, "it_scroll_light");
        run(engine, "draw_magic()");
        runSeconds(engine, 2.0f);
        left = engine.lightSecondsLeft();
        REQUIRE(left > 280.0f);
        auto state = engine.captureGame();
        REQUIRE_MESSAGE(state.ok(), (state.ok() ? "" : state.error().message));
        REQUIRE(state.value().hero.has_value());
        CHECK(state.value().hero->light == "spl_light");
        CHECK(state.value().hero->lightSeconds == doctest::Approx(left));
        json = save::toJson(state.value());
    }
    Engine engine(lightConfig());
    REQUIRE(engine.init().ok());
    runSeconds(engine, 0.1f);
    CHECK_FALSE(engine.lightOrbLight().has_value());
    auto state = save::fromJson(json);
    REQUIRE_MESSAGE(state.ok(), (state.ok() ? "" : state.error().message));
    REQUIRE(engine.applyGame(state.value()).ok());
    CHECK(engine.lightSecondsLeft() == doctest::Approx(left).epsilon(0.001));
    runSeconds(engine, 1.0f);
    REQUIRE(engine.lightOrbLight().has_value());
    CHECK(engine.lightSecondsLeft() == doctest::Approx(left - 1.0f).epsilon(0.01));
    CHECK(glm::length(*engine.lightOrbPosition() - overHand(engine)) < 0.05f);

    // A save without a light (older ones have none) puts none on.
    auto withoutLight = state.value();
    withoutLight.hero->light.clear();
    REQUIRE(engine.applyGame(withoutLight).ok());
    runSeconds(engine, 0.1f);
    CHECK_FALSE(engine.lightOrbLight().has_value());
}
