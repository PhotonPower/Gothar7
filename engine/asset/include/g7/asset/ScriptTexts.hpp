// Translations of the content texts of the scripts: assets/source/text/script.<language>.json (M14 part G,
// contract docs/coordination.md "Inhaltstexte"; written by tools/voice, `gothar-voice texts`). The German
// texts stay inline in Lua (decision E2); the engine shows the translation of a German text when the UI
// language is not German. Entries without a translation (status "offen") or with a changed German text
// ("veraltet") are left out: the German text shows instead.
#pragma once

#include <g7/core/Result.hpp>

#include <string>
#include <string_view>
#include <unordered_map>

namespace g7::asset
{
class ScriptTexts
{
public:
    /// script.<language>.json, schema 1. Errors name `source`.
    [[nodiscard]] static Result<ScriptTexts> parse(std::string_view json, std::string_view source);

    /// The translation of the German `text`; nullptr if there is none.
    [[nodiscard]] const std::string* find(std::string_view german) const;
    /// The translation stored under `key` ("it_apple.name", "dia_gate_guard_hello_01"); nullptr if none.
    [[nodiscard]] const std::string* findKey(std::string_view key) const;
    [[nodiscard]] const std::string& language() const noexcept { return m_language; }
    [[nodiscard]] std::size_t size() const noexcept { return m_byKey.size(); }

private:
    std::string m_language;
    std::unordered_map<std::string, std::string> m_byGerman;
    std::unordered_map<std::string, std::string> m_byKey;
};
} // namespace g7::asset
