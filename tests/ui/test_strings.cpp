// UI texts by key (M14 part A): lookup, fallback to the key, placeholders, number format; the shipped
// tables for German and English have the same keys, and every key the code names exists.

#include <g7/core/FileSystem.hpp>
#include <g7/ui/Strings.hpp>

#include <doctest/doctest.h>

#include <filesystem>
#include <limits>
#include <ostream>
#include <regex>
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

TEST_CASE("Strings: every key the code, the UI data and the scripts name is in the tables (M14 part G)")
{
    const fs::Path assets = fs::fromUtf8(G7_ASSET_SOURCE_DIR);
    const fs::Path root = assets.parent_path().parent_path();
    auto de = fs::readText(assets / "text" / "ui.de.toml");
    REQUIRE(de);
    auto german = Strings::load("de", de.value(), "ui.de.toml");
    REQUIRE(german);
    const std::regex quotedKey(R"re("(UI_[A-Z0-9_]*[A-Z0-9])")re");
    usize found = 0;
    for (const fs::Path& dir : {root / "engine", root / "game" / "scripts", assets / "ui"})
    {
        for (const auto& entry : std::filesystem::recursive_directory_iterator(dir))
        {
            const std::string ext = entry.path().extension().string();
            if (!entry.is_regular_file() ||
                (ext != ".cpp" && ext != ".hpp" && ext != ".lua" && ext != ".toml"))
            {
                continue;
            }
            auto text = fs::readText(entry.path());
            REQUIRE(text);
            for (std::sregex_iterator it(text.value().begin(), text.value().end(), quotedKey), end; it != end;
                 ++it)
            {
                const std::string key = (*it)[1].str();
                ++found;
                CHECK_MESSAGE(german.value().contains(key), key, " (", fs::toUtf8(entry.path()), ")");
            }
        }
    }
    CHECK(found > 100);
    for (int n = 1; n <= 6; ++n) // "UI_ORDINAL_{}" (circles of magic)
    {
        CHECK(german.value().contains("UI_ORDINAL_" + std::to_string(n)));
    }
}
