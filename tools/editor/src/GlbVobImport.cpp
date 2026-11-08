#include <g7/core/StringUtil.hpp>
#include <g7/editor/GlbVobImport.hpp>

#include <algorithm>
#include <format>

namespace g7::editor
{
namespace
{
constexpr std::string_view kPrefix = "VOB_";
constexpr f32 kLightRangePerUnit = 5.0f; ///< m of light range per unit of the empty's size

/// "Name.001" -> "Name" (Blender numbers duplicates).
std::string_view withoutBlenderNumber(std::string_view name)
{
    const usize dot = name.rfind('.');
    if (dot != std::string_view::npos && dot + 1 < name.size() &&
        std::ranges::all_of(name.substr(dot + 1), [](char c) { return c >= '0' && c <= '9'; }))
    {
        return name.substr(0, dot);
    }
    return name;
}
} // namespace

VobImport vobsFromMarkers(std::span<const VobMarker> markers)
{
    VobImport out;
    for (const VobMarker& m : markers)
    {
        const std::string_view full = withoutBlenderNumber(m.name);
        if (!full.starts_with(kPrefix))
        {
            continue;
        }
        const std::string_view rest = full.substr(kPrefix.size());
        const usize split = rest.find('_');
        const std::string type = toUpper(rest.substr(0, split));
        const std::string name =
            split == std::string_view::npos ? std::string() : toUpper(rest.substr(split + 1));
        if (name.empty())
        {
            out.warnings.push_back(std::format("{}: needs VOB_<type>_<name>", m.name));
            continue;
        }
        // Placement without scale: the scale sizes the volume (trigger) or the reach (light).
        const Vec3 scale(glm::length(Vec3(m.local[0])), glm::length(Vec3(m.local[1])),
                         glm::length(Vec3(m.local[2])));
        world::WorldFileVob v;
        v.name = name;
        v.transform.position = Vec3(m.local[3]);
        if (scale.x > 1e-6f && scale.y > 1e-6f && scale.z > 1e-6f)
        {
            v.transform.rotation = glm::normalize(glm::quat_cast(
                Mat3(Vec3(m.local[0]) / scale.x, Vec3(m.local[1]) / scale.y, Vec3(m.local[2]) / scale.z)));
        }
        if (type == "LIGHT")
        {
            v.type = world::VobType::Light;
            v.light.range = std::max(scale.x, std::max(scale.y, scale.z)) * kLightRangePerUnit;
        }
        else if (type == "START")
        {
            v.type = world::VobType::Start;
        }
        else if (type == "TRIGGER")
        {
            v.type = world::VobType::Trigger;
            v.trigger.shape = world::TriggerVolume::Shape::Box;
            v.trigger.halfExtents = glm::max(scale, Vec3(0.05f));
        }
        else
        {
            out.warnings.push_back(
                std::format("{}: unknown type '{}' (LIGHT, START, TRIGGER)", m.name, type));
            continue;
        }
        out.vobs.push_back(std::move(v));
    }
    return out;
}
} // namespace g7::editor
