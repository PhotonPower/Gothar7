// Translations of the content texts (text/script.<language>.json, M14 part G): found by the German text or
// the key; missing and outdated ones are left out; the repository's English file parses and translates every
// text.

#include <g7/asset/ScriptTexts.hpp>

#include <doctest/doctest.h>

#include <fstream>
#include <ostream> // doctest needs it to print std::string operands
#include <sstream>
#include <string>

using namespace g7;
using asset::ScriptTexts;

TEST_CASE("Script texts: translation by German text and by key, open and outdated ones left out")
{
    auto texts = ScriptTexts::parse(R"({"schema": 1, "lang": "en", "texts": {
  "it_apple.name": {"de": "Apfel", "text": "Apple", "context": "items/food.lua", "status": "entwurf"},
  "it_bread.name": {"de": "Brot", "text": "", "status": "offen"},
  "it_ham.name": {"de": "Schinken neu", "text": "Ham", "status": "veraltet"},
  "dia_guard_hello_01": {"de": "Halt!", "text": "Stop!", "status": "abgenommen"}
}})",
                                    "script.en.json");
    REQUIRE_MESSAGE(texts.ok(), (texts.ok() ? "" : texts.error().message));
    const ScriptTexts& t = texts.value();
    CHECK(t.language() == "en");
    CHECK(t.size() == 2);
    REQUIRE(t.find("Apfel") != nullptr);
    CHECK(*t.find("Apfel") == "Apple");
    CHECK(*t.find("Halt!") == "Stop!");
    CHECK(*t.findKey("dia_guard_hello_01") == "Stop!");
    CHECK(t.find("Brot") == nullptr);
    CHECK(t.find("Schinken neu") == nullptr);
    CHECK(t.findKey("it_ham.name") == nullptr);

    CHECK_FALSE(ScriptTexts::parse("[]", "x").ok());
    CHECK_FALSE(ScriptTexts::parse(R"({"schema": 2, "texts": {}})", "x").ok());
    CHECK_FALSE(ScriptTexts::parse(R"({"schema": 1, "texts": {"a": {"text": "x"}}})", "x").ok());
}

TEST_CASE("Script texts: the shipped English translation parses and has every text")
{
    std::ifstream file(G7_ASSET_SOURCE_DIR "/text/script.en.json", std::ios::binary);
    REQUIRE(file.good());
    std::stringstream buffer;
    buffer << file.rdbuf();
    const std::string json = buffer.str();
    auto texts = ScriptTexts::parse(json, "script.en.json");
    REQUIRE_MESSAGE(texts.ok(), (texts.ok() ? "" : texts.error().message));
    CHECK(texts.value().language() == "en");
    // gothar-voice check (CI) keeps the file in line with the scripts; here: nothing is left open.
    CHECK(json.find(R"("status": "offen")") == std::string::npos);
    CHECK(json.find(R"("status": "veraltet")") == std::string::npos);
    REQUIRE(texts.value().find("Apfel") != nullptr);
    CHECK(*texts.value().find("Apfel") == "Apple");
}
