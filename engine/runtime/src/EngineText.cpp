// Localisation in the engine (M14 part G, docs/modules/ui.md "Lokalisierung"): the UI texts by key
// (text/ui.<language>.toml, also headless: notices and diary lines come from them) and the translations of
// the content texts of the scripts (text/script.<language>.json; the German texts stay inline in Lua, E2).
// The game data stays German (Story, dialogue menus, item names); screens translate when they show it.

#include <g7/core/Log.hpp>
#include <g7/runtime/Engine.hpp>

#include <format>

namespace g7
{
std::string Engine::language() const
{
    return m_config.settings.get<std::string>("ui.language", "de");
}

void Engine::loadTexts()
{
    const std::string lang = language();
    const auto readText = [this](const std::string& path) -> Result<std::string>
    {
        auto bytes = m_vfs.read(path);
        if (!bytes)
        {
            return bytes.error();
        }
        return std::string(reinterpret_cast<const char*>(bytes.value().data()), bytes.value().size());
    };
    const std::string uiPath = std::format("text/ui.{}.toml", lang);
    auto uiToml = readText(uiPath);
    auto strings =
        uiToml ? ui::Strings::load(lang, uiToml.value(), uiPath) : Result<ui::Strings>(uiToml.error());
    if (strings)
    {
        m_uiStrings = std::move(strings).value();
    }
    else
    {
        G7_LOG_ERROR("engine", "{}: {} (UI texts show their keys)", uiPath, strings.error().message);
        m_uiStrings = ui::Strings();
    }

    m_scriptTexts = asset::ScriptTexts();
    m_untranslated.clear();
    if (lang == "de")
    {
        return; // the scripts' own language
    }
    const std::string scriptPath = std::format("text/script.{}.json", lang);
    auto json = readText(scriptPath);
    auto texts =
        json ? asset::ScriptTexts::parse(json.value(), scriptPath) : Result<asset::ScriptTexts>(json.error());
    if (!texts)
    {
        G7_LOG_WARN("engine", "{} (content texts stay German)", texts.error().message);
        return;
    }
    m_scriptTexts = std::move(texts).value();
    G7_LOG_INFO("engine", "{}: {} translated content texts", scriptPath, m_scriptTexts.size());
}

std::string Engine::contentText(std::string_view german) const
{
    if (german.empty() || language() == "de")
    {
        return std::string(german);
    }
    if (const std::string* text = m_scriptTexts.find(german))
    {
        return *text;
    }
    if (m_untranslated.insert(std::string(german)).second)
    {
        G7_LOG_DEBUG("engine", "no {} translation: \"{}\"", language(),
                     german); // also texts built in the UI language
    }
    return std::string(german);
}

std::string Engine::uiText(std::string_view key, std::initializer_list<ui::TextArg> args) const
{
    return m_uiStrings.format(key, args);
}

std::string Engine::castBlockedText(const gameplay::CastBlock& block) const
{
    if (block.reason == gameplay::CastBlock::Reason::Circle)
    {
        return uiText("UI_CAST_CIRCLE", {{"ordinal", uiText(std::format("UI_ORDINAL_{}", block.circle))}});
    }
    return uiText("UI_CAST_MANA");
}
} // namespace g7
