#include "GltfExtras.hpp"

#include <g7/core/Log.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstring>

namespace g7::asset
{
std::optional<WindKind> parseWindKind(std::string_view text) noexcept
{
    for (const WindKind kind :
         {WindKind::Trunk, WindKind::Leaves, WindKind::Grass, WindKind::Cloth, WindKind::Hanging})
    {
        if (text == windKindName(kind))
        {
            return kind;
        }
    }
    return std::nullopt;
}

std::string_view windKindName(WindKind kind) noexcept
{
    switch (kind)
    {
    case WindKind::Trunk:
        return "trunk";
    case WindKind::Leaves:
        return "leaves";
    case WindKind::Grass:
        return "grass";
    case WindKind::Cloth:
        return "cloth";
    case WindKind::Hanging:
        return "hanging";
    case WindKind::None:
        break;
    }
    return "none";
}

std::string_view gltfJsonText(std::span<const u8> bytes) noexcept
{
    const auto u32At = [&](usize offset)
    {
        u32 value = 0;
        std::memcpy(&value, bytes.data() + offset, 4);
        return value;
    };
    if (bytes.size() >= 20 && std::memcmp(bytes.data(), "glTF", 4) == 0)
    {
        const u32 length = u32At(12);
        if (u32At(16) == 0x4E4F534Au && 20 + static_cast<usize>(length) <= bytes.size()) // "JSON"
        {
            return {reinterpret_cast<const char*>(bytes.data() + 20), length};
        }
        return {};
    }
    return {reinterpret_cast<const char*>(bytes.data()), bytes.size()};
}

void readMaterialWind(std::span<const u8> bytes, std::vector<MaterialInfo>& materials,
                      std::string_view debugName)
{
    using Json = nlohmann::json;
    const std::string_view text = gltfJsonText(bytes);
    if (text.find("\"wind") == std::string_view::npos)
    {
        return; // most models: no need to parse the JSON a second time
    }
    const Json doc = Json::parse(text.begin(), text.end(), nullptr, false);
    if (doc.is_discarded() || !doc.contains("materials") || !doc["materials"].is_array())
    {
        return;
    }
    const Json& list = doc["materials"];
    for (usize i = 0; i < list.size() && i < materials.size(); ++i)
    {
        const Json& material = list[i];
        if (!material.is_object() || !material.contains("extras") || !material["extras"].is_object())
        {
            continue;
        }
        const Json& extras = material["extras"];
        MaterialInfo& info = materials[i];
        const auto warn = [&](std::string_view what)
        { G7_LOG_WARN("asset", "{}: material '{}': extras.{} (still)", debugName, info.name, what); };
        if (!extras.contains("wind"))
        {
            continue;
        }
        const Json& wind = extras["wind"];
        const auto kind = wind.is_string() ? parseWindKind(wind.get<std::string>()) : std::nullopt;
        if (!kind)
        {
            warn("wind: trunk, leaves, grass, cloth or hanging");
            continue;
        }
        info.wind = *kind;
        if (extras.contains("wind_strength"))
        {
            const Json& strength = extras["wind_strength"];
            if (strength.is_number())
            {
                info.windStrength = std::clamp(strength.get<f32>(), 0.0f, kMaxWindStrength);
            }
            else
            {
                warn("wind_strength: a number 0..2 (1 instead)");
            }
        }
        if (extras.contains("wind_pivot"))
        {
            const Json& pivot = extras["wind_pivot"];
            if (pivot.is_array() && pivot.size() == 3 &&
                std::all_of(pivot.begin(), pivot.end(), [](const Json& v) { return v.is_number(); }))
            {
                info.windPivot = Vec3(pivot[0].get<f32>(), pivot[1].get<f32>(), pivot[2].get<f32>());
            }
            else
            {
                warn("wind_pivot: [x, y, z] (the origin instead)");
            }
        }
    }
}
} // namespace g7::asset
