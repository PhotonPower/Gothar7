#include <g7/asset/VoiceLines.hpp>

#include <nlohmann/json.hpp>

#include <format>

namespace g7::asset
{
namespace
{
using Json = nlohmann::json;

/// "dia_gate_guard_hello_01" -> "dia_gate_guard_hello"; "svm_guard_m_thief_01" -> "svm_guard_m".
std::string groupOf(const std::string& key)
{
    const auto lastUnderscore = key.rfind('_');
    const std::string withoutNumber =
        lastUnderscore == std::string::npos ? key : key.substr(0, lastUnderscore);
    if (!key.starts_with("svm_"))
    {
        return withoutNumber;
    }
    // svm_<voice>_<gender>_<occasion>: the voice and the gender
    const auto voiceEnd = key.find('_', 4);
    const auto genderEnd = voiceEnd == std::string::npos ? std::string::npos : key.find('_', voiceEnd + 1);
    return genderEnd == std::string::npos ? withoutNumber : key.substr(0, genderEnd);
}
} // namespace

Result<VoiceLines> VoiceLines::parse(std::string_view json, std::string_view source)
{
    const Json root = Json::parse(json, nullptr, false);
    if (root.is_discarded() || !root.is_object())
    {
        return Error{std::format("{}: no JSON object", source)};
    }
    if (root.value("schema", 0) != 1)
    {
        return Error{std::format("{}: schema must be 1", source)};
    }
    const auto lines = root.find("lines");
    if (lines == root.end() || !lines->is_object())
    {
        return Error{std::format("{}: 'lines' must be an object", source)};
    }
    VoiceLines out;
    for (const auto& [key, line] : lines->items())
    {
        if (!line.is_object() || !line.contains("text") || !line["text"].is_string())
        {
            return Error{std::format("{}: lines.{}: needs 'text'", source, key)};
        }
        if (line.value("orphan", false))
        {
            continue; // no longer in the scripts
        }
        out.m_groups[groupOf(key)].push_back({line["text"].get<std::string>(), key});
        ++out.m_size;
    }
    return out;
}

std::optional<std::string> VoiceLines::dialogKey(std::string_view info, std::string_view text) const
{
    if (const auto group = m_groups.find(std::string(info)); group != m_groups.end())
    {
        for (const Entry& e : group->second)
        {
            if (e.text == text)
            {
                return e.key;
            }
        }
    }
    return std::nullopt;
}

std::optional<std::string> VoiceLines::shoutKey(std::string_view voice, std::string_view gender,
                                                std::string_view text) const
{
    return dialogKey(std::format("svm_{}_{}", voice, gender), text);
}
} // namespace g7::asset
