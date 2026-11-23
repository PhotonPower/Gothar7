#include <g7/core/StringUtil.hpp>
#include <g7/editor/DungeonEdit.hpp>
#include <g7/editor/GlbVobImport.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <format>

namespace g7::editor
{
namespace
{
using Json = nlohmann::json;

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

/// How an extras value is read before it goes into the component.
enum class Value : u8
{
    Plain,   ///< as it is (text, number, list of numbers)
    Flag,    ///< true/false; Blender's 0/1 accepted
    Whole,   ///< a whole number (Blender may write 2.0)
    Targets, ///< a list of names; one text "A, B" accepted (Blender has no lists of texts)
};

/// One extras key: where it goes in the component (`into`: empty = the component itself, else a sub-object
/// such as "changeWorld", "plate", "teleport") and under which key.
struct Field
{
    std::string_view key;
    Value value = Value::Plain;
    std::string_view into;
    std::string_view as; ///< empty: the same key
};

constexpr std::array kTriggerFields = {
    Field{"onEnter"},
    Field{"onLeave"},
    Field{"filter"},
    Field{"once", Value::Flag},
    Field{"owner"},
    Field{"targets", Value::Targets},
    Field{"action"},
    Field{"world", Value::Plain, "changeWorld"},
    Field{"start", Value::Plain, "changeWorld"},
    Field{"fall", Value::Flag, "changeWorld"},
};
constexpr std::array kPlateFields = {
    Field{"filter"},
    Field{"once", Value::Flag},
    Field{"targets", Value::Targets},
    Field{"action"},
    Field{"weight", Value::Plain, "plate"},
    Field{"release_action", Value::Plain, "plate"},
};
constexpr std::array kTeleportFields = {
    Field{"to", Value::Plain, "teleport"},
    Field{"world", Value::Plain, "teleport"},
    Field{"start", Value::Plain, "teleport"},
    Field{"filter", Value::Plain, "teleport"},
    Field{"visible", Value::Flag, "teleport"},
    Field{"active", Value::Flag, "teleport"},
    Field{"yaw", Value::Plain, "teleport"},
    Field{"targets", Value::Targets},
    Field{"action"},
    Field{"once", Value::Flag},
};
constexpr std::array kRelayFields = {
    Field{"mode"},
    Field{"count", Value::Whole},
    Field{"delay"},
    Field{"once", Value::Flag},
    Field{"invert", Value::Flag},
    Field{"targets", Value::Targets},
    Field{"action"},
};
constexpr std::array kSpawnFields = {
    Field{"npc"},
    Field{"count", Value::Whole},
    Field{"respawn", Value::Flag},
    Field{"dormant", Value::Flag},
};
constexpr std::array kLightFields = {
    Field{"color"}, Field{"range"}, Field{"intensity"}, Field{"flicker"}, Field{"daylight", Value::Flag},
};
constexpr std::array kMoverFields = {
    Field{"kind"},       Field{"open", Value::Flag},
    Field{"seconds"},    Field{"offset"},
    Field{"axis"},       Field{"angle"},
    Field{"crush"},      Field{"sound_move"},
    Field{"sound_stop"},
};

/// The value as the component wants it (see Value); a value it cannot turn is passed on for the reader's
/// error.
Json normalized(const Json& in, Value kind)
{
    switch (kind)
    {
    case Value::Flag:
        if (in.is_number())
        {
            return in.get<double>() != 0.0;
        }
        break;
    case Value::Whole:
        if (in.is_number_float() && in.get<double>() >= 0.0 &&
            std::floor(in.get<double>()) == in.get<double>())
        {
            return static_cast<u64>(in.get<double>());
        }
        break;
    case Value::Targets:
        if (in.is_string())
        {
            Json list = Json::array();
            for (std::string& name : splitNames(in.get<std::string>()))
            {
                list.push_back(std::move(name));
            }
            return list;
        }
        break;
    case Value::Plain:
        break;
    }
    return in;
}

/// Puts the extras into `component` by `fields`; unknown keys (not starting with "_") are warned about.
void applyExtras(const Json& extras, std::span<const Field> fields, Json& component, std::string_view node,
                 std::vector<std::string>& warnings)
{
    for (const auto& [key, value] : extras.items())
    {
        const auto field = std::ranges::find(fields, std::string_view(key), &Field::key);
        if (field == fields.end())
        {
            if (!key.starts_with('_'))
            {
                warnings.push_back(std::format("{}: extras '{}' is not a field here (left out)", node, key));
            }
            continue;
        }
        Json& target = field->into.empty() ? component : component[std::string(field->into)];
        target[std::string(field->as.empty() ? field->key : field->as)] = normalized(value, field->value);
    }
}

/// The vob with `component` of its JSON replaced by `filled` (read with the file's checks).
Result<world::WorldFileVob> withComponent(world::WorldFileVob vob, std::string_view key, const Json& filled,
                                          std::string_view node)
{
    const world::VobId id = vob.id;
    vob.id = world::VobId{1}; // the reader wants an id
    Json json = Json::parse(world::writeVobJson(vob));
    json["components"][std::string(key)] = filled;
    auto read = world::parseVobJson(json.dump(), node);
    if (!read)
    {
        return read.error();
    }
    read.value().id = id;
    return read;
}

/// The extras object of a node, or an empty object (with a warning if it is no object).
Json extrasOf(std::string_view text, std::string_view node, std::vector<std::string>& warnings)
{
    if (text.empty())
    {
        return Json::object();
    }
    Json extras = Json::parse(text.begin(), text.end(), nullptr, false);
    if (extras.is_discarded() || !extras.is_object())
    {
        warnings.push_back(std::format("{}: extras are no JSON object (left out)", node));
        return Json::object();
    }
    return extras;
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
        std::string_view component;
        std::span<const Field> fields;
        Json filled = Json::object();
        const auto boxTrigger = [&]
        {
            v.type = world::VobType::Trigger;
            v.trigger.shape = world::TriggerVolume::Shape::Box;
            v.trigger.halfExtents = glm::max(scale, Vec3(0.05f));
            component = "trigger";
        };
        if (type == "LIGHT")
        {
            v.type = world::VobType::Light;
            v.light.range = std::max(scale.x, std::max(scale.y, scale.z)) * kLightRangePerUnit;
            component = "light";
            fields = kLightFields;
        }
        else if (type == "START")
        {
            v.type = world::VobType::Start;
        }
        else if (type == "TRIGGER")
        {
            boxTrigger();
            fields = kTriggerFields;
        }
        else if (type == "PLATE")
        {
            boxTrigger();
            fields = kPlateFields;
            filled["plate"] = Json::object();
        }
        else if (type == "TELEPORT")
        {
            boxTrigger();
            fields = kTeleportFields;
            filled["teleport"] = Json::object();
        }
        else if (type == "RELAY")
        {
            v.type = world::VobType::Relay;
            component = "relay";
            fields = kRelayFields;
        }
        else if (type == "SPAWN")
        {
            v.type = world::VobType::Spawn;
            component = "spawn";
            fields = kSpawnFields;
        }
        else
        {
            out.warnings.push_back(
                std::format("{}: unknown type '{}' (LIGHT, START, TRIGGER, PLATE, TELEPORT, RELAY, SPAWN)",
                            m.name, type));
            continue;
        }
        const Json extras = extrasOf(m.extras, m.name, out.warnings);
        if (component.empty())
        {
            if (!extras.empty())
            {
                out.warnings.push_back(std::format("{}: a {} takes no extras (left out)", m.name, type));
            }
            out.vobs.push_back(std::move(v));
            continue;
        }
        // Start from what the file would hold (a trigger's box), then lay the extras over it.
        Json base = Json::parse(world::writeVobJson(
            [&]
            {
                world::WorldFileVob copy = v;
                copy.id = world::VobId{1};
                return copy;
            }()));
        if (base.contains("components") && base["components"].contains(std::string(component)))
        {
            Json merged = base["components"][std::string(component)];
            merged.update(filled);
            filled = std::move(merged);
        }
        applyExtras(extras, fields, filled, m.name, out.warnings);
        auto read = withComponent(v, component, filled, m.name);
        if (!read)
        {
            out.warnings.push_back(std::format("{} (skipped)", read.error().message));
            continue;
        }
        out.vobs.push_back(std::move(read).value());
    }
    return out;
}

std::optional<world::Mover> moverFromExtras(std::string_view extras, std::string_view node,
                                            std::vector<std::string>& warnings)
{
    const Json all = extrasOf(extras, node, warnings);
    if (!all.contains("mover"))
    {
        return std::nullopt;
    }
    const Json& given = all["mover"];
    Json filled = Json::object();
    if (given.is_string())
    {
        filled["kind"] = given; // just the kind: everything else from its defaults
    }
    else if (given.is_object())
    {
        applyExtras(given, kMoverFields, filled, node, warnings);
    }
    else
    {
        warnings.push_back(std::format("{}: extras 'mover' must be its kind or an object (left out)", node));
        return std::nullopt;
    }
    world::WorldFileVob probe;
    probe.type = world::VobType::Mesh;
    probe.mesh = "mover.glb";
    auto read = withComponent(probe, "mover", filled, node);
    if (!read)
    {
        warnings.push_back(std::format("{} (no mover)", read.error().message));
        return std::nullopt;
    }
    return read.value().mover;
}
} // namespace g7::editor
