#include <g7/asset/Vfs.hpp>
#include <g7/core/StringUtil.hpp>
#include <g7/world/Scene.hpp>
#include <g7/world/WorldFile.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <format>
#include <unordered_map>
#include <unordered_set>

namespace g7::world
{
namespace
{
using Json = nlohmann::ordered_json; // keeps key order: stable output

constexpr std::array<std::pair<VobType, std::string_view>, 7> kVobTypes = {{{VobType::Empty, "empty"},
                                                                            {VobType::Mesh, "mesh"},
                                                                            {VobType::Light, "light"},
                                                                            {VobType::Start, "start"},
                                                                            {VobType::Sound, "sound"},
                                                                            {VobType::Trigger, "trigger"},
                                                                            {VobType::Mob, "mob"}}};
constexpr std::array<std::pair<SoundEmitter::Mode, std::string_view>, 2> kSoundModes = {
    {{SoundEmitter::Mode::Loop, "loop"}, {SoundEmitter::Mode::Random, "random"}}};
constexpr std::array<std::pair<TriggerVolume::Filter, std::string_view>, 3> kTriggerFilters = {
    {{TriggerVolume::Filter::Player, "player"},
     {TriggerVolume::Filter::Npc, "npc"},
     {TriggerVolume::Filter::Any, "any"}}};

template <typename E, usize N>
std::string_view nameOf(const std::array<std::pair<E, std::string_view>, N>& table, E value)
{
    for (const auto& [v, name] : table)
    {
        if (v == value)
        {
            return name;
        }
    }
    return table[0].second;
}

/// Reads JSON values with errors that name the file and the entry.
struct Reader
{
    std::string_view source;

    Error error(std::string_view where, std::string_view what) const
    {
        return Error{std::format("{}: {}: {}", source, where, what)};
    }

    Result<std::vector<f32>> numbers(const Json& value, std::string_view where, usize count) const
    {
        if (!value.is_array() || value.size() != count)
        {
            return error(where, std::format("must be a list of {} numbers", count));
        }
        std::vector<f32> result;
        for (const Json& n : value)
        {
            if (!n.is_number())
            {
                return error(where, std::format("must be a list of {} numbers", count));
            }
            result.push_back(n.get<f32>());
        }
        return result;
    }

    Result<Vec3> vec3(const Json& object, const char* key, std::string_view where, Vec3 fallback) const
    {
        if (!object.contains(key))
        {
            return fallback;
        }
        auto v = numbers(object[key], std::format("{}.{}", where, key), 3);
        if (!v)
        {
            return v.error();
        }
        return Vec3(v.value()[0], v.value()[1], v.value()[2]);
    }

    Result<f32> number(const Json& object, const char* key, std::string_view where, f32 fallback) const
    {
        if (!object.contains(key))
        {
            return fallback;
        }
        if (!object[key].is_number())
        {
            return error(std::format("{}.{}", where, key), "must be a number");
        }
        return object[key].get<f32>();
    }
};

/// components.<key> of a vob, or an empty object.
Json componentOf(const Json& v, const char* key)
{
    return v.contains("components") && v["components"].is_object() && v["components"].contains(key)
               ? v["components"][key]
               : Json::object();
}

Result<std::string> readText(const Reader& r, const Json& object, const char* key, std::string_view where,
                             bool required)
{
    if (!object.contains(key))
    {
        return required ? Result<std::string>(r.error(where, std::format("needs '{}'", key))) : std::string();
    }
    if (!object[key].is_string() || (required && object[key].get<std::string>().empty()))
    {
        return r.error(where, std::format("'{}' must be a {}string", key, required ? "non-empty " : ""));
    }
    return object[key].get<std::string>();
}

template <typename E, usize N>
Result<E> readChoice(const Reader& r, const Json& object, const char* key, std::string_view where,
                     const std::array<std::pair<E, std::string_view>, N>& table)
{
    if (!object.contains(key))
    {
        return table[0].first;
    }
    const std::string value = object[key].is_string() ? object[key].get<std::string>() : std::string();
    for (const auto& [e, name] : table)
    {
        if (name == value)
        {
            return e;
        }
    }
    std::string choices;
    for (const auto& [e, name] : table)
    {
        choices += (choices.empty() ? "'" : ", '") + std::string(name) + "'";
    }
    return r.error(where, std::format("'{}' must be one of {}", key, choices));
}

Result<SoundEmitter> readSound(const Reader& r, const Json& v, std::string_view where)
{
    const Json s = componentOf(v, "sound");
    const std::string at = std::format("{}.components.sound", where);
    SoundEmitter sound;
    auto name = readText(r, s, "sound", at, true);
    if (!name)
    {
        return name.error();
    }
    sound.sound = std::move(name).value();
    auto range = r.number(s, "range", at, sound.range);
    auto volume = r.number(s, "volume", at, sound.volume);
    auto mode = readChoice(r, s, "mode", at, kSoundModes);
    if (!range || !volume || !mode)
    {
        return !range ? range.error() : !volume ? volume.error() : mode.error();
    }
    if (!(range.value() > 0.0f))
    {
        return r.error(at, "'range' must be positive");
    }
    if (volume.value() < 0.0f || volume.value() > 1.0f)
    {
        return r.error(at, "'volume' must lie in 0..1");
    }
    sound.range = range.value();
    sound.volume = volume.value();
    sound.mode = mode.value();
    if (s.contains("delay"))
    {
        auto delay = r.numbers(s["delay"], std::format("{}.delay", at), 2);
        if (!delay)
        {
            return delay.error();
        }
        if (delay.value()[0] < 0.0f || delay.value()[1] < delay.value()[0])
        {
            return r.error(at, "'delay' must be [min, max] seconds with 0 <= min <= max");
        }
        sound.delay = Vec2(delay.value()[0], delay.value()[1]);
    }
    return sound;
}

Result<TriggerVolume> readTrigger(const Reader& r, const Json& v, std::string_view where)
{
    const Json t = componentOf(v, "trigger");
    const std::string at = std::format("{}.components.trigger", where);
    TriggerVolume trigger;
    const std::string shape = t.value("shape", std::string("box"));
    if (shape == "box")
    {
        auto half = r.vec3(t, "halfExtents", at, trigger.halfExtents);
        if (!half)
        {
            return half.error();
        }
        if (!(half.value().x > 0.0f && half.value().y > 0.0f && half.value().z > 0.0f))
        {
            return r.error(at, "'halfExtents' must be positive");
        }
        trigger.halfExtents = half.value();
    }
    else if (shape == "sphere")
    {
        trigger.shape = TriggerVolume::Shape::Sphere;
        auto radius = r.number(t, "radius", at, trigger.radius);
        if (!radius)
        {
            return radius.error();
        }
        if (!(radius.value() > 0.0f))
        {
            return r.error(at, "'radius' must be positive");
        }
        trigger.radius = radius.value();
    }
    else
    {
        return r.error(at, "'shape' must be 'box' or 'sphere'");
    }
    auto onEnter = readText(r, t, "onEnter", at, false);
    auto onLeave = readText(r, t, "onLeave", at, false);
    auto filter = readChoice(r, t, "filter", at, kTriggerFilters);
    if (!onEnter || !onLeave || !filter)
    {
        return !onEnter ? onEnter.error() : !onLeave ? onLeave.error() : filter.error();
    }
    trigger.onEnter = std::move(onEnter).value();
    trigger.onLeave = std::move(onLeave).value();
    trigger.filter = filter.value();
    if (t.contains("once"))
    {
        if (!t["once"].is_boolean())
        {
            return r.error(at, "'once' must be true or false");
        }
        trigger.once = t["once"].get<bool>();
    }
    if (t.contains("target"))
    {
        // Reserved: a vob id or a vob name.
        if (t["target"].is_number_unsigned() && t["target"].get<u64>() > 0)
        {
            trigger.targetId = VobId{t["target"].get<u64>()};
        }
        else if (t["target"].is_string() && !t["target"].get<std::string>().empty())
        {
            trigger.targetName = t["target"].get<std::string>();
        }
        else
        {
            return r.error(at, "'target' must be a vob id or a vob name");
        }
    }
    return trigger;
}

Result<WorldFileVob> readVob(const Reader& r, const Json& v, std::string_view where)
{
    if (!v.is_object())
    {
        return r.error(where, "must be an object");
    }
    WorldFileVob vob;
    if (!v.contains("id") || !v["id"].is_number_unsigned() || v["id"].get<u64>() == 0)
    {
        return r.error(where, "needs an 'id' (integer >= 1)");
    }
    vob.id = VobId{v["id"].get<u64>()};
    const std::string type = v.value("type", std::string("empty"));
    const auto known =
        std::find_if(kVobTypes.begin(), kVobTypes.end(), [&](const auto& t) { return t.second == type; });
    if (known == kVobTypes.end())
    {
        return r.error(where, std::format("unknown type '{}'", type));
    }
    vob.type = known->first;
    if (v.contains("name") && !v["name"].is_string())
    {
        return r.error(std::format("{}.name", where), "must be a string");
    }
    vob.name = v.value("name", std::string());
    if (v.contains("parent"))
    {
        if (!v["parent"].is_number_unsigned())
        {
            return r.error(std::format("{}.parent", where), "must be a vob id");
        }
        vob.parent = VobId{v["parent"].get<u64>()};
    }

    auto position = r.vec3(v, "pos", where, Vec3(0.0f));
    auto scale = r.vec3(v, "scale", where, Vec3(1.0f));
    if (!position || !scale)
    {
        return !position ? position.error() : scale.error();
    }
    vob.transform.position = position.value();
    vob.transform.scale = scale.value();
    if (v.contains("rot"))
    {
        auto q = r.numbers(v["rot"], std::format("{}.rot", where), 4); // quaternion x, y, z, w
        if (!q)
        {
            return q.error();
        }
        vob.transform.rotation = glm::normalize(Quat(q.value()[3], q.value()[0], q.value()[1], q.value()[2]));
    }

    if (vob.type == VobType::Mesh || vob.type == VobType::Mob)
    {
        if (!v.contains("mesh") || !v["mesh"].is_string() || v["mesh"].get<std::string>().empty())
        {
            return r.error(where, std::format("a {} vob needs 'mesh' (VFS path)", type));
        }
        vob.mesh = v["mesh"].get<std::string>();
        if (v.contains("category"))
        {
            const std::string category =
                v["category"].is_string() ? v["category"].get<std::string>() : std::string();
            if (category != "deco" && category != "gameplay")
            {
                return r.error(where, "'category' must be 'deco' or 'gameplay'");
            }
            if (vob.type == VobType::Mob && category == "deco")
            {
                return r.error(where, "a mob vob is always 'gameplay'");
            }
            vob.category = category == "gameplay" ? VobCategory::Gameplay : VobCategory::Deco;
        }
        if (vob.type == VobType::Mob)
        {
            vob.category = VobCategory::Gameplay;
        }
    }
    if (vob.type == VobType::Light)
    {
        const Json light = v.contains("components") && v["components"].contains("light")
                               ? v["components"]["light"]
                               : Json::object();
        const std::string at = std::format("{}.components.light", where);
        auto color = r.vec3(light, "color", at, vob.light.color);
        auto range = r.number(light, "range", at, vob.light.range);
        auto intensity = r.number(light, "intensity", at, vob.light.intensity);
        auto flicker = r.number(light, "flicker", at, vob.light.flicker);
        if (!color || !range || !intensity || !flicker)
        {
            return !color       ? color.error()
                   : !range     ? range.error()
                   : !intensity ? intensity.error()
                                : flicker.error();
        }
        if (range.value() <= 0.0f)
        {
            return r.error(at, "'range' must be positive");
        }
        vob.light = {color.value(), range.value(), intensity.value(), flicker.value()};
    }
    if (vob.type == VobType::Sound)
    {
        auto sound = readSound(r, v, where);
        if (!sound)
        {
            return sound.error();
        }
        vob.sound = std::move(sound).value();
    }
    if (vob.type == VobType::Trigger)
    {
        auto trigger = readTrigger(r, v, where);
        if (!trigger)
        {
            return trigger.error();
        }
        vob.trigger = std::move(trigger).value();
    }
    if (vob.type == VobType::Mob)
    {
        auto definition =
            readText(r, componentOf(v, "mob"), "definition", std::format("{}.components.mob", where), true);
        if (!definition)
        {
            return definition.error();
        }
        vob.mob.definition = std::move(definition).value();
    }
    return vob;
}

/// Numbers rounded to 1e-5 (0.01 mm, quaternions well within float noise): float rounding like
/// -4.37e-08 for a zero would make every save differ; reading and writing again stays identical.
double tidy(f32 value)
{
    const double rounded = std::round(static_cast<double>(value) * 1e5) / 1e5;
    return rounded == 0.0 ? 0.0 : rounded; // no -0
}

Result<void> readSplat(const Reader& r, const Json& s, TerrainRef& ref)
{
    const std::string where = "terrain.splat";
    if (!s.is_object())
    {
        return r.error(where, "must be an object with 'maps' and 'layers'");
    }
    if (!s.contains("layers") || !s["layers"].is_array() || s["layers"].empty() ||
        s["layers"].size() > kMaxTerrainLayers)
    {
        return r.error(where, std::format("needs 'layers': a list of 1 to {}", kMaxTerrainLayers));
    }
    const usize mapsNeeded = (s["layers"].size() + 3) / 4;
    if (!s.contains("maps") || !s["maps"].is_array() || s["maps"].size() != mapsNeeded)
    {
        return r.error(where, std::format("{} layers need 'maps': a list of {} VFS path(s)",
                                          s["layers"].size(), mapsNeeded));
    }
    for (const Json& map : s["maps"])
    {
        if (!map.is_string() || map.get<std::string>().empty())
        {
            return r.error(where, "'maps' must hold VFS paths");
        }
        ref.splatMaps.push_back(map.get<std::string>());
    }
    for (usize i = 0; i < s["layers"].size(); ++i)
    {
        const Json& l = s["layers"][i];
        const std::string at = std::format("terrain.splat.layers[{}]", i);
        if (!l.is_object())
        {
            return r.error(at, "must be an object");
        }
        TerrainLayerRef layer;
        for (auto [key, target] : {std::pair{"name", &layer.name}, std::pair{"albedo", &layer.albedo}})
        {
            if (!l.contains(key) || !l[key].is_string() || l[key].get<std::string>().empty())
            {
                return r.error(at, std::format("needs '{}'", key));
            }
            *target = l[key].get<std::string>();
        }
        if (l.contains("tile"))
        {
            if (!l["tile"].is_number() || !(l["tile"].get<f32>() > 0.0f))
            {
                return r.error(at, "'tile' must be a positive number (metres)");
            }
            layer.tile = l["tile"].get<f32>();
        }
        if (l.contains("normal"))
        {
            if (!l["normal"].is_string())
            {
                return r.error(at, "'normal' must be a VFS path");
            }
            layer.normal = l["normal"].get<std::string>();
        }
        ref.layers.push_back(std::move(layer));
    }
    return {};
}

Result<TerrainRef> readTerrain(const Reader& r, const Json& t)
{
    const std::string where = "terrain";
    if (!t.is_object())
    {
        return r.error(where, "must be an object");
    }
    if (!t.contains("version") || !t["version"].is_number_unsigned())
    {
        return r.error(where, "needs a 'version'");
    }
    if (t["version"].get<u32>() != kTerrainVersion)
    {
        return r.error(where, std::format("version {} is not supported (expected {})",
                                          t["version"].get<u32>(), kTerrainVersion));
    }
    TerrainRef ref;
    if (!t.contains("heightmap") || !t["heightmap"].is_string() || t["heightmap"].get<std::string>().empty())
    {
        return r.error(where, "needs 'heightmap' (VFS path)");
    }
    ref.heightmap = t["heightmap"].get<std::string>();
    for (auto [key, target] : {std::pair{"width", &ref.width}, std::pair{"height", &ref.height}})
    {
        if (!t.contains(key) || !t[key].is_number_unsigned() || t[key].get<u32>() < 2)
        {
            return r.error(where, std::format("'{}' must be an integer >= 2", key));
        }
        *target = t[key].get<u32>();
    }
    for (auto [key, target] :
         {std::pair{"cellSize", &ref.cellSize}, std::pair{"minY", &ref.minY}, std::pair{"maxY", &ref.maxY}})
    {
        if (!t.contains(key) || !t[key].is_number())
        {
            return r.error(where, std::format("needs '{}' (number)", key));
        }
        *target = t[key].get<f32>();
    }
    if (!(ref.cellSize > 0.0f))
    {
        return r.error(where, "'cellSize' must be positive");
    }
    if (!(ref.maxY > ref.minY))
    {
        return r.error(where, "'maxY' must be above 'minY'");
    }
    if (!t.contains("firstSample"))
    {
        return r.error(where, "needs 'firstSample' [x, z]");
    }
    auto first = r.numbers(t["firstSample"], "terrain.firstSample", 2);
    if (!first)
    {
        return first.error();
    }
    ref.firstSample = Vec2(first.value()[0], first.value()[1]);
    if (t.contains("splat"))
    {
        auto splat = readSplat(r, t["splat"], ref);
        if (!splat)
        {
            return splat.error();
        }
    }
    if (t.contains("holes"))
    {
        if (!t["holes"].is_string() || t["holes"].get<std::string>().empty())
        {
            return r.error(where, "'holes' must be a VFS path");
        }
        ref.holes = t["holes"].get<std::string>();
    }
    return ref;
}

Json numbers(std::initializer_list<f32> values)
{
    Json array = Json::array();
    for (const f32 v : values)
    {
        array.push_back(tidy(v));
    }
    return array;
}
} // namespace

std::string_view vobTypeName(VobType type) noexcept
{
    for (const auto& [t, name] : kVobTypes)
    {
        if (t == type)
        {
            return name;
        }
    }
    return "empty";
}

Result<WorldFile> parseWorldFile(std::string_view text, std::string_view source)
{
    const Json root = Json::parse(text.begin(), text.end(), nullptr, false);
    const Reader r{source};
    if (root.is_discarded())
    {
        return r.error("file", "not valid JSON");
    }
    if (!root.is_object())
    {
        return r.error("file", "must be a JSON object");
    }
    if (!root.contains("version") || !root["version"].is_number_unsigned())
    {
        return r.error("version", "missing");
    }
    if (root["version"].get<u32>() != kWorldFileVersion)
    {
        return r.error("version", std::format("{} is not supported (expected {})", root["version"].get<u32>(),
                                              kWorldFileVersion));
    }
    WorldFile world;
    world.name = root.value("name", std::string());
    if (root.contains("nextVobId"))
    {
        if (!root["nextVobId"].is_number_unsigned())
        {
            return r.error("nextVobId", "must be an integer");
        }
        world.nextVobId = root["nextVobId"].get<u64>();
    }
    if (root.contains("staticMeshes"))
    {
        for (const Json& mesh : root["staticMeshes"])
        {
            if (!mesh.is_string())
            {
                return r.error("staticMeshes", "must be a list of VFS paths");
            }
            world.staticMeshes.push_back(mesh.get<std::string>());
        }
    }
    if (root.contains("terrain"))
    {
        auto terrain = readTerrain(r, root["terrain"]);
        if (!terrain)
        {
            return terrain.error();
        }
        world.terrain = std::move(terrain).value();
    }
    if (root.contains("vobs"))
    {
        if (!root["vobs"].is_array())
        {
            return r.error("vobs", "must be a list");
        }
        std::unordered_set<u64> ids;
        std::unordered_map<std::string, usize>
            startNames; // lower case -> index (--start must be unambiguous)
        for (usize i = 0; i < root["vobs"].size(); ++i)
        {
            auto vob = readVob(r, root["vobs"][i], std::format("vobs[{}]", i));
            if (!vob)
            {
                return vob.error();
            }
            if (!ids.insert(vob.value().id.value).second)
            {
                return r.error(std::format("vobs[{}]", i),
                               std::format("duplicate id {}", vob.value().id.value));
            }
            if (vob.value().type == VobType::Start)
            {
                if (vob.value().name.empty())
                {
                    return r.error(std::format("vobs[{}]", i),
                                   "a start vob needs a 'name' (--start selects it)");
                }
                if (const auto [it, added] = startNames.emplace(toLower(vob.value().name), i); !added)
                {
                    return r.error(std::format("vobs[{}]", i),
                                   std::format("start point name '{}' is already used by vobs[{}]",
                                               vob.value().name, it->second));
                }
            }
            world.vobs.push_back(std::move(vob).value());
        }
    }
    // The counter must lie above every id in the file.
    for (const WorldFileVob& vob : world.vobs)
    {
        world.nextVobId = std::max(world.nextVobId, vob.id.value + 1);
    }
    if (root.contains("waynet"))
    {
        world.waynetJson = root["waynet"].dump();
    }
    if (root.contains("zones"))
    {
        world.zonesJson = root["zones"].dump();
    }
    return world;
}

Result<WorldFile> loadWorldFile(const asset::Vfs& vfs, std::string_view path)
{
    auto bytes = vfs.read(path);
    if (!bytes)
    {
        return bytes.error();
    }
    return parseWorldFile(
        std::string_view(reinterpret_cast<const char*>(bytes.value().data()), bytes.value().size()), path);
}

std::string writeWorldFile(const WorldFile& world)
{
    Json root = Json::object();
    root["version"] = kWorldFileVersion;
    root["name"] = world.name;
    root["nextVobId"] = world.nextVobId;
    root["staticMeshes"] = world.staticMeshes;
    if (world.terrain)
    {
        const TerrainRef& t = *world.terrain;
        root["terrain"] = Json{{"version", kTerrainVersion},
                               {"heightmap", t.heightmap},
                               {"width", t.width},
                               {"height", t.height},
                               {"cellSize", tidy(t.cellSize)},
                               {"firstSample", numbers({t.firstSample.x, t.firstSample.y})},
                               {"minY", tidy(t.minY)},
                               {"maxY", tidy(t.maxY)}};
        if (!t.layers.empty())
        {
            Json layers = Json::array();
            for (const TerrainLayerRef& layer : t.layers)
            {
                Json l = Json{{"name", layer.name}, {"albedo", layer.albedo}, {"tile", tidy(layer.tile)}};
                if (!layer.normal.empty())
                {
                    l["normal"] = layer.normal;
                }
                layers.push_back(std::move(l));
            }
            root["terrain"]["splat"] = Json{{"maps", t.splatMaps}, {"layers", std::move(layers)}};
        }
        if (!t.holes.empty())
        {
            root["terrain"]["holes"] = t.holes;
        }
    }

    std::vector<const WorldFileVob*> sorted;
    for (const WorldFileVob& vob : world.vobs)
    {
        sorted.push_back(&vob);
    }
    std::sort(sorted.begin(), sorted.end(), [](const auto* a, const auto* b) { return a->id < b->id; });
    Json vobs = Json::array();
    for (const WorldFileVob* vob : sorted)
    {
        Json v = Json::object();
        v["id"] = vob->id.value;
        v["type"] = vobTypeName(vob->type);
        v["name"] = vob->name;
        if (vob->parent.valid())
        {
            v["parent"] = vob->parent.value;
        }
        const Transform& t = vob->transform;
        v["pos"] = numbers({t.position.x, t.position.y, t.position.z});
        v["rot"] = numbers({t.rotation.x, t.rotation.y, t.rotation.z, t.rotation.w});
        if (t.scale != Vec3(1.0f))
        {
            v["scale"] = numbers({t.scale.x, t.scale.y, t.scale.z});
        }
        if (vob->type == VobType::Mesh || vob->type == VobType::Mob)
        {
            v["mesh"] = vob->mesh;
            if (vob->type == VobType::Mesh && vob->category == VobCategory::Gameplay)
            {
                v["category"] = "gameplay"; // deco is the default and not written
            }
        }
        if (vob->type == VobType::Light)
        {
            const LightSource& l = vob->light;
            v["components"]["light"] = Json{{"color", numbers({l.color.r, l.color.g, l.color.b})},
                                            {"range", tidy(l.range)},
                                            {"intensity", tidy(l.intensity)},
                                            {"flicker", tidy(l.flicker)}};
        }
        if (vob->type == VobType::Sound)
        {
            const SoundEmitter& s = vob->sound;
            v["components"]["sound"] = Json{{"sound", s.sound},
                                            {"range", tidy(s.range)},
                                            {"volume", tidy(s.volume)},
                                            {"mode", nameOf(kSoundModes, s.mode)},
                                            {"delay", numbers({s.delay.x, s.delay.y})}};
        }
        if (vob->type == VobType::Trigger)
        {
            const TriggerVolume& tv = vob->trigger;
            Json trigger = Json::object();
            if (tv.shape == TriggerVolume::Shape::Box)
            {
                trigger["shape"] = "box";
                trigger["halfExtents"] = numbers({tv.halfExtents.x, tv.halfExtents.y, tv.halfExtents.z});
            }
            else
            {
                trigger["shape"] = "sphere";
                trigger["radius"] = tidy(tv.radius);
            }
            if (!tv.onEnter.empty())
            {
                trigger["onEnter"] = tv.onEnter;
            }
            if (!tv.onLeave.empty())
            {
                trigger["onLeave"] = tv.onLeave;
            }
            trigger["filter"] = nameOf(kTriggerFilters, tv.filter);
            trigger["once"] = tv.once;
            if (tv.targetId.valid())
            {
                trigger["target"] = tv.targetId.value;
            }
            else if (!tv.targetName.empty())
            {
                trigger["target"] = tv.targetName;
            }
            v["components"]["trigger"] = std::move(trigger);
        }
        if (vob->type == VobType::Mob)
        {
            v["components"]["mob"] = Json{{"definition", vob->mob.definition}};
        }
        vobs.push_back(std::move(v));
    }
    // Layout for diffs: the header keys one per line, then one vob per line, so changing a vob
    // changes exactly one line. Waynet and zones follow as compact values.
    std::string out = "{\n";
    for (const auto& [key, value] : root.items())
    {
        out += "  \"" + key + "\": " + value.dump() + ",\n";
    }
    out += "  \"vobs\": [";
    for (usize i = 0; i < vobs.size(); ++i)
    {
        out += (i == 0 ? "\n    " : ",\n    ") + vobs[i].dump();
    }
    out += vobs.empty() ? "]" : "\n  ]";
    if (!world.waynetJson.empty())
    {
        out += ",\n  \"waynet\": " + Json::parse(world.waynetJson, nullptr, false).dump();
    }
    if (!world.zonesJson.empty())
    {
        out += ",\n  \"zones\": " + Json::parse(world.zonesJson, nullptr, false).dump();
    }
    out += "\n}\n";
    return out;
}

Result<void> spawnWorld(Scene& scene, const WorldFile& world)
{
    // Check everything first, so a broken file leaves the scene untouched.
    std::unordered_map<u64, const WorldFileVob*> byId;
    for (const WorldFileVob& vob : world.vobs)
    {
        if (!byId.emplace(vob.id.value, &vob).second || scene.findById(vob.id) != entt::null)
        {
            return Error{std::format("{}: duplicate vob id {}", world.name, vob.id.value)};
        }
        if (vob.id.runtime())
        {
            return Error{std::format("{}: vob id {} lies in the runtime range", world.name, vob.id.value)};
        }
    }
    // Parents before children, whatever the file order: depth = length of the parent chain.
    std::unordered_map<u64, usize> depth;
    for (const WorldFileVob& vob : world.vobs)
    {
        usize d = 0;
        for (VobId p = vob.parent; p.valid(); ++d)
        {
            const auto parent = byId.find(p.value);
            if (parent == byId.end())
            {
                return Error{
                    std::format("{}: vob {} has unknown parent {}", world.name, vob.id.value, p.value)};
            }
            if (d > world.vobs.size())
            {
                return Error{std::format("{}: parent cycle at vob {}", world.name, vob.id.value)};
            }
            p = parent->second->parent;
        }
        depth[vob.id.value] = d;
    }
    std::vector<const WorldFileVob*> order;
    for (const WorldFileVob& vob : world.vobs)
    {
        order.push_back(&vob);
    }
    std::stable_sort(order.begin(), order.end(),
                     [&](const auto* a, const auto* b) { return depth[a->id.value] < depth[b->id.value]; });

    for (const WorldFileVob* vob : order)
    {
        auto e = scene.spawnVob({vob->name, vob->transform, vob->parent, vob->id});
        if (!e)
        {
            return e.error(); // not expected after the checks above
        }
        switch (vob->type)
        {
        case VobType::Empty:
            break;
        case VobType::Mesh:
            scene.set<MeshRef>(e.value(), {vob->mesh, vob->category});
            break;
        case VobType::Light:
            scene.set<LightSource>(e.value(), vob->light);
            break;
        case VobType::Start:
            scene.set<StartPoint>(e.value(), {});
            break;
        case VobType::Sound:
            scene.set<SoundEmitter>(e.value(), vob->sound);
            break;
        case VobType::Trigger:
            scene.set<TriggerVolume>(e.value(), vob->trigger);
            break;
        case VobType::Mob:
            scene.set<MeshRef>(e.value(), {vob->mesh, VobCategory::Gameplay});
            scene.set<MobRef>(e.value(), vob->mob);
            break;
        }
    }
    if (world.nextVobId > scene.nextVobId())
    {
        if (auto raised = scene.setNextVobId(world.nextVobId); !raised)
        {
            return raised;
        }
    }
    return {};
}

WorldFile captureWorld(const Scene& scene, std::string_view name)
{
    WorldFile world;
    world.name = std::string(name);
    world.nextVobId = scene.nextVobId();
    scene.each<Vob, Transform>(
        [&](entt::entity e, const Vob& vob, const Transform& transform)
        {
            if (vob.id.runtime())
            {
                return;
            }
            WorldFileVob out;
            out.id = vob.id;
            out.name = vob.nameText;
            out.parent = scene.idOf(scene.parent(e));
            out.transform = transform;
            if (const MobRef* mob = scene.get<MobRef>(e))
            {
                out.type = VobType::Mob;
                out.mob = *mob;
                const MeshRef* mesh = scene.get<MeshRef>(e);
                out.mesh = mesh != nullptr ? mesh->path : std::string();
            }
            else if (const MeshRef* mesh = scene.get<MeshRef>(e))
            {
                out.type = VobType::Mesh;
                out.mesh = mesh->path;
                out.category = mesh->category;
            }
            else if (const LightSource* light = scene.get<LightSource>(e))
            {
                out.type = VobType::Light;
                out.light = *light;
            }
            else if (scene.has<StartPoint>(e))
            {
                out.type = VobType::Start;
            }
            else if (const SoundEmitter* sound = scene.get<SoundEmitter>(e))
            {
                out.type = VobType::Sound;
                out.sound = *sound;
            }
            else if (const TriggerVolume* trigger = scene.get<TriggerVolume>(e))
            {
                out.type = VobType::Trigger;
                out.trigger = *trigger;
            }
            world.vobs.push_back(std::move(out));
        });
    return world;
}
} // namespace g7::world
