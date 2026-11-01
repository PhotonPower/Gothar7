#include <g7/asset/ScriptTexts.hpp>

#include <nlohmann/json.hpp>

#include <format>

namespace g7::asset
{
Result<ScriptTexts> ScriptTexts::parse(std::string_view json, std::string_view source)
{
    using Json = nlohmann::json;
    const Json root = Json::parse(json, nullptr, false);
    if (root.is_discarded() || !root.is_object())
    {
        return Error{std::format("{}: no JSON object", source)};
    }
    if (root.value("schema", 0) != 1)
    {
        return Error{std::format("{}: schema must be 1", source)};
    }
    const auto texts = root.find("texts");
    if (texts == root.end() || !texts->is_object())
    {
        return Error{std::format("{}: 'texts' must be an object", source)};
    }
    ScriptTexts out;
    out.m_language = root.value("lang", std::string());
    for (const auto& [key, entry] : texts->items())
    {
        if (!entry.is_object() || !entry.contains("de") || !entry["de"].is_string() ||
            !entry.value("text", Json()).is_string())
        {
            return Error{std::format("{}: texts.{}: needs 'de' and 'text'", source, key)};
        }
        const std::string status = entry.value("status", std::string("offen"));
        std::string text = entry["text"].get<std::string>();
        if (text.empty() || status == "offen" || status == "veraltet")
        {
            continue;
        }
        out.m_byGerman.try_emplace(entry["de"].get<std::string>(), text);
        out.m_byKey.emplace(key, std::move(text));
    }
    return out;
}

const std::string* ScriptTexts::find(std::string_view german) const
{
    const auto it = m_byGerman.find(std::string(german));
    return it == m_byGerman.end() ? nullptr : &it->second;
}

const std::string* ScriptTexts::findKey(std::string_view key) const
{
    const auto it = m_byKey.find(std::string(key));
    return it == m_byKey.end() ? nullptr : &it->second;
}
} // namespace g7::asset
