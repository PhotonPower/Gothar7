#include <g7/core/StringUtil.hpp>

#include <doctest/doctest.h>

#include <string>
#include <unordered_map>
#include <vector>

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

namespace
{
std::vector<u32> decodeAll(std::string_view s)
{
    std::vector<u32> out;
    for (usize pos = 0; pos < s.size();)
    {
        out.push_back(nextCodepoint(s, pos));
    }
    return out;
}
} // namespace

TEST_CASE("StringUtil: UTF-8 decoding")
{
    CHECK(decodeAll("Ab") == std::vector<u32>{'A', 'b'});
    CHECK(decodeAll("\xC3\xA4\xC3\x9F") == std::vector<u32>{0xE4, 0xDF});             // ä ß
    CHECK(decodeAll("\xE2\x80\xA6\xE2\x82\xAC") == std::vector<u32>{0x2026, 0x20AC}); // … €
    CHECK(decodeAll("\xF0\x9F\x98\x80") == std::vector<u32>{0x1F600});
    // Malformed input: one replacement per bad byte, the rest still decodes.
    CHECK(decodeAll("a\xFFz") == std::vector<u32>{'a', kReplacementCharacter, 'z'});
    CHECK(decodeAll("\xC3") == std::vector<u32>{kReplacementCharacter});       // truncated
    CHECK(decodeAll("\xC3(") == std::vector<u32>{kReplacementCharacter, '('}); // bad continuation
    CHECK(decodeAll("\xC0\xAF") ==
          std::vector<u32>{kReplacementCharacter, kReplacementCharacter}); // overlong
    CHECK(decodeAll("\xED\xA0\x80").front() == kReplacementCharacter);     // surrogate
}

TEST_CASE("StringUtil: UTF-8 encoding round trip")
{
    std::string s;
    for (const u32 c : {0x41u, 0xE4u, 0x2026u, 0x1F600u})
    {
        appendUtf8(s, c);
    }
    CHECK(s == "A\xC3\xA4\xE2\x80\xA6\xF0\x9F\x98\x80");
    CHECK(decodeAll(s) == std::vector<u32>{0x41, 0xE4, 0x2026, 0x1F600});
    std::string bad;
    appendUtf8(bad, 0xD800);
    CHECK(bad == "\xEF\xBF\xBD");
}
