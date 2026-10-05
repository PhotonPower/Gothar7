// Voice lines (voice/lines.<language>.json, docs/modules/audio.md "Sprache"): the key of a spoken line from
// its text - dialogue from (Info, text), shouts from (voice, gender, text); the repository's database parses.

#include <g7/asset/VoiceLines.hpp>

#include <doctest/doctest.h>

#include <fstream>
#include <ostream> // doctest needs it to print std::string operands
#include <sstream>
#include <string>

using namespace g7;
using asset::VoiceLines;

namespace
{
constexpr const char* kLines = R"({"schema": 1, "lang": "de", "lines": {
  "dia_guard_hello_01": {"text": "Halt!", "speaker": "npc_guard"},
  "dia_guard_hello_02": {"text": "Ich geh ja schon.", "speaker": "hero"},
  "dia_guard_hello_old_01": {"text": "Halt!", "speaker": "npc_guard"},
  "dia_guard_gone_01": {"text": "Vorbei.", "orphan": true},
  "svm_guard_m_thief_01": {"text": "Dieb!", "speaker": "svm"},
  "svm_farmer_f_thief_01": {"text": "Dieb!", "speaker": "svm"},
  "svm_guard_m_weapon_warn_02": {"text": "Letzte Warnung!", "speaker": "svm"}
}})";
} // namespace

TEST_CASE("Voice lines: keys from Info and text, shouts from voice, gender and text")
{
    auto lines = VoiceLines::parse(kLines, "lines.de.json");
    REQUIRE_MESSAGE(lines.ok(), (lines.ok() ? "" : lines.error().message));
    const VoiceLines& v = lines.value();
    CHECK(v.size() == 6); // the orphan does not count
    CHECK(v.dialogKey("dia_guard_hello", "Halt!") == "dia_guard_hello_01");
    CHECK(v.dialogKey("dia_guard_hello", "Ich geh ja schon.") == "dia_guard_hello_02");
    CHECK(v.dialogKey("dia_guard_hello_old", "Halt!") == "dia_guard_hello_old_01"); // its own Info
    CHECK_FALSE(v.dialogKey("dia_guard_hello", "Was anderes.").has_value());
    CHECK_FALSE(v.dialogKey("dia_guard_gone", "Vorbei.").has_value());
    CHECK(v.shoutKey("guard", "m", "Dieb!") == "svm_guard_m_thief_01");
    CHECK(v.shoutKey("farmer", "f", "Dieb!") == "svm_farmer_f_thief_01");
    CHECK(v.shoutKey("guard", "m", "Letzte Warnung!") == "svm_guard_m_weapon_warn_02");
    CHECK_FALSE(v.shoutKey("farmer", "m", "Dieb!").has_value());

    CHECK_FALSE(VoiceLines::parse("[]", "x").ok());
    CHECK_FALSE(VoiceLines::parse(R"({"schema": 2, "lines": {}})", "x").ok());
    CHECK_FALSE(VoiceLines::parse(R"({"schema": 1, "lines": {"a_01": {"speaker": "x"}}})", "x").ok());
}

TEST_CASE("Voice lines: the repository's database parses")
{
    std::ifstream file(G7_ASSET_SOURCE_DIR "/voice/lines.de.json", std::ios::binary);
    REQUIRE(file.good());
    std::stringstream text;
    text << file.rdbuf();
    auto lines = VoiceLines::parse(text.str(), "lines.de.json");
    REQUIRE_MESSAGE(lines.ok(), (lines.ok() ? "" : lines.error().message));
    CHECK(lines.value().size() > 100);
    CHECK(
        lines.value().dialogKey("dia_gate_guard_hello", "Moment mal. Dich hab ich hier noch nie gesehen.") ==
        "dia_gate_guard_hello_01");
}
