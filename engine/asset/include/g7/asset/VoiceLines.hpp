// Spoken lines and their keys: assets/source/voice/lines.<language>.json (contract docs/modules/audio.md
// "Sprache"; written by tools/voice, `gothar-voice scan`). The texts stay inline in the scripts (decision
// E2); the engine finds the key of a line from its text: dialogue lines "<info>_NN" from (Info, text), shouts
// "svm_<voice>_<m|f>_<occasion>_NN" from (voice, gender, text). With the key come the voice file and later
// the translation.
#pragma once

#include <g7/core/Result.hpp>

#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace g7::asset
{
class VoiceLines
{
public:
    /// lines.<language>.json, schema 1. Errors name `source`.
    [[nodiscard]] static Result<VoiceLines> parse(std::string_view json, std::string_view source);

    /// The key of `text` said in the Info `info` ("dia_gate_guard_hello_01"); nullopt if the database does
    /// not know it (`gothar-voice scan --write` not run after a change).
    [[nodiscard]] std::optional<std::string> dialogKey(std::string_view info, std::string_view text) const;
    /// The key of the shout `text` in the voice `voice` ("guard") and `gender` ("m"/"f").
    [[nodiscard]] std::optional<std::string> shoutKey(std::string_view voice, std::string_view gender,
                                                      std::string_view text) const;
    [[nodiscard]] std::size_t size() const noexcept { return m_size; }

private:
    struct Entry
    {
        std::string text;
        std::string key;
    };
    // Dialogue: the Info ("dia_gate_guard_hello") -> its lines; shouts: "svm_<voice>_<gender>" -> its lines.
    std::unordered_map<std::string, std::vector<Entry>> m_groups;
    std::size_t m_size = 0;
};
} // namespace g7::asset
