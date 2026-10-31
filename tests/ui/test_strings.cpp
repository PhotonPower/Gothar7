// UI texts by key (M14 part A): lookup, fallback to the key, placeholders, number format; the shipped
// tables for German and English have the same keys.

#include <g7/core/FileSystem.hpp>
#include <g7/ui/Strings.hpp>

#include <doctest/doctest.h>

#include <limits>
#include <ostream>
#include <string>

using namespace g7;
using namespace g7::ui;

TEST_CASE("Strings: lookup, missing keys, placeholders")
{
    auto strings = Strings::load("de", "UI_TITLE = \"Inventar\"\nUI_XP = \"Erfahrung +{xp}\"\n");
    REQUIRE(strings);
    const Strings& s = strings.value();
    CHECK(s.language() == "de");
    CHECK(s.get("UI_TITLE") == "Inventar");
    CHECK(s.contains("UI_XP"));
    CHECK(s.get("UI_MISSING") == "UI_MISSING"); // the key shows
    CHECK(s.format("UI_XP", {{"xp", "50"}}) == "Erfahrung +50");
    CHECK(s.format("UI_XP", {{"other", "1"}}) == "Erfahrung +{xp}");
    CHECK(s.keys() == std::set<std::string>{"UI_TITLE", "UI_XP"});
    CHECK(substitute("{a}{b}{a}", {{"a", "x"}, {"b", "y"}}) == "xyx");
    CHECK(substitute("{open", {{"open", "x"}}) == "{open");
    CHECK_FALSE(Strings::load("de", "UI_NUMBER = 5\n"));
}

TEST_CASE("Strings: numbers per language")
{
    CHECK(formatNumber(0, "de") == "0");
    CHECK(formatNumber(999, "de") == "999");
    CHECK(formatNumber(1234, "de") == "1.234");
    CHECK(formatNumber(1234567, "en") == "1,234,567");
    CHECK(formatNumber(-50000, "de") == "-50.000");
    CHECK(formatNumber(std::numeric_limits<i64>::min(), "en") == "-9,223,372,036,854,775,808");
}

TEST_CASE("Strings: the shipped German and English tables match")
{
    const fs::Path dir = fs::fromUtf8(G7_ASSET_SOURCE_DIR) / "text";
    auto de = fs::readText(dir / "ui.de.toml");
    auto en = fs::readText(dir / "ui.en.toml");
    REQUIRE(de);
    REQUIRE(en);
    auto german = Strings::load("de", de.value(), "ui.de.toml");
    auto english = Strings::load("en", en.value(), "ui.en.toml");
    REQUIRE_MESSAGE(german, (german ? "" : german.error().message));
    REQUIRE_MESSAGE(english, (english ? "" : english.error().message));
    CHECK(german.value().keys() == english.value().keys());
    CHECK_FALSE(german.value().keys().empty());
}
