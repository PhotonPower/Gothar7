// Magic (M12 part B, label "headless"): the spells, runes and scrolls of the content load; who may cast what
// - runes need the spell's magic circle, scrolls not, both the mana (Z1-Z3).

#include <g7/runtime/Engine.hpp>

#include <doctest/doctest.h>

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
} // namespace

TEST_CASE("Engine magic: the starting spells load; runes need the circle, scrolls not, both the mana")
{
    EngineConfig config;
    config.appName = "magic";
    config.headless = true;
    config.world = fs::fromUtf8("testworld/camp.g7world");
    config.start = "START_LAGER";
    Engine engine(std::move(config));
    REQUIRE(engine.init().ok());
    CHECK(engine.scripts()->errors().empty());
    for (const char* rune : {"it_rune_firebolt", "it_rune_heal", "it_rune_sleep", "it_rune_transform_wolf",
                             "it_rune_summon_wolf"})
    {
        const auto spell = engine.spellOfItem(rune);
        REQUIRE_MESSAGE(spell.has_value(), rune);
        CHECK(spell->mana > 0);
    }
    const auto sleep = engine.spellOfItem("it_rune_sleep");
    CHECK(sleep->circle == 2);
    CHECK(sleep->effect == "sleep");
    CHECK(sleep->duration == doctest::Approx(20.0f));
    CHECK(engine.spellOfItem("it_rune_summon_wolf")->summon == "mon_wolf");
    CHECK(engine.spellOfItem("it_rune_firebolt")->damage.at("fire") == 25);
    CHECK_FALSE(engine.spellOfItem("it_apple").has_value());

    run(engine, "give_item('it_rune_sleep') give_item('it_scroll_sleep') set_stat('mana_max', 30) "
                "set_stat('mana', 30)");
    // Z2: the rune wants the second circle.
    CHECK(run(engine, "cast_check('it_rune_sleep')").asString() ==
          "Dafür fehlt dir der zweite Kreis der Magie.");
    CHECK(run(engine, "cast_check('it_scroll_sleep')").isNil()); // Z3: a scroll without a circle
    run(engine, "set_talent('magic_circle', 2)");
    CHECK(run(engine, "cast_check('it_rune_sleep')").isNil());
    // Z1/Z3: the mana, from a rune and from a scroll alike.
    run(engine, "set_stat('mana', 10)");
    CHECK(run(engine, "cast_check('it_rune_sleep')").asString() == "Dafür reicht dein Mana nicht.");
    CHECK(run(engine, "cast_check('it_scroll_sleep')").asString() == "Dafür reicht dein Mana nicht.");
    CHECK(run(engine, "cast_check('it_rune_heal')").asString() == "Das hast du nicht.");
}
