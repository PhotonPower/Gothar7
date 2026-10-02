#include <g7/core/StringUtil.hpp>

#include <doctest/doctest.h>

#include <string>
#include <unordered_map>

using namespace g7;

TEST_CASE("StringUtil: ASCII case conversion")
{
    static_assert(toLowerAscii('A') == 'a');
    static_assert(toUpperAscii('z') == 'Z');
    static_assert(toLowerAscii('_') == '_');
    CHECK(toLower("ItMw_1H_Sword") == "itmw_1h_sword");
    CHECK(toUpper("pc_hero") == "PC_HERO");
}

TEST_CASE("StringUtil: non-ASCII bytes pass through unchanged")
{
    const std::string umlauts = "\xC3\x84pfel \xC3\xBC"; // "Äpfel ü" in UTF-8
    CHECK(toLower(umlauts) == "\xC3\x84pfel \xC3\xBC");
    CHECK(toUpper(umlauts) == "\xC3\x84PFEL \xC3\xBC");
    CHECK(equalsIgnoreCase(umlauts, "\xC3\x84PFEL \xC3\xBC"));
    CHECK_FALSE(equalsIgnoreCase("\xC3\xA4", "\xC3\x84")); // ä vs Ä: not folded
}

TEST_CASE("StringUtil: equalsIgnoreCase / startsWithIgnoreCase")
{
    static_assert(equalsIgnoreCase("Diego", "DIEGO"));
    CHECK(equalsIgnoreCase("", ""));
    CHECK_FALSE(equalsIgnoreCase("Diego", "Diegos"));
    CHECK_FALSE(equalsIgnoreCase("Diego", "Milten"));

    CHECK(startsWithIgnoreCase("ZS_TalkToPlayer", "zs_"));
    CHECK(startsWithIgnoreCase("abc", ""));
    CHECK_FALSE(startsWithIgnoreCase("ZS", "ZS_Talk"));
    CHECK_FALSE(startsWithIgnoreCase("B_Attack", "ZS_"));
}

TEST_CASE("StringUtil: case-insensitive unordered_map")
{
    std::unordered_map<std::string, int, IgnoreCaseHash, IgnoreCaseEqual> guilds;
    guilds["Diego"] = 1;
    CHECK(guilds.count("DIEGO") == 1);
    CHECK(guilds.at("diego") == 1);
    guilds["DIEGO"] = 2; // same key
    CHECK(guilds.size() == 1);
    CHECK(guilds.at("Diego") == 2);
    CHECK(IgnoreCaseHash{}("Milten") == IgnoreCaseHash{}("MILTEN"));
}
