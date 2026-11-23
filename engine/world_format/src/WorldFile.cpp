#include <g7/asset/Vfs.hpp>
#include <g7/core/Log.hpp>
#include <g7/core/StringUtil.hpp>
#include <g7/world/WorldFile.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <charconv>
#include <cmath>
#include <format>
#include <unordered_map>
#include <unordered_set>

namespace g7::world
{
namespace
{
using Json = nlohmann::ordered_json; // keeps key order: stable output

constexpr std::array<std::pair<VobType, std::string_view>, 10> kVobTypes = {{{VobType::Empty, "empty"},
                                                                             {VobType::Mesh, "mesh"},
                                                                             {VobType::Light, "light"},
                                                                             {VobType::Start, "start"},
                                                                             {VobType::Sound, "sound"},
                                                                             {VobType::Trigger, "trigger"},
                                                                             {VobType::Mob, "mob"},
                                                                             {VobType::Water, "water"},
                                                                             {VobType::Item, "item"},
                                                                             {VobType::Relay, "relay"}}};
constexpr std::array<std::pair<SoundEmitter::Mode, std::string_view>, 2> kSoundModes = {
    {{SoundEmitter::Mode::Loop, "loop"}, {SoundEmitter::Mode::Random, "random"}}};
constexpr std::array<std::pair<TriggerVolume::Filter, std::string_view>, 3> kTriggerFilters = {
    {{TriggerVolume::Filter::Player, "player"},
     {TriggerVolume::Filter::Npc, "npc"},
     {TriggerVolume::Filter::Any, "any"}}};
constexpr std::array<std::pair<Relay::Mode, std::string_view>, 3> kRelayModes = {
    {{Relay::Mode::Any, "any"}, {Relay::Mode::All, "all"}, {Relay::Mode::Count, "count"}}};
constexpr std::array<std::pair<Mover::Kind, std::string_view>, 3> kMoverKinds = {
    {{Mover::Kind::Gate, "gate"},
     {Mover::Kind::SecretWall, "secret_wall"},
     {Mover::Kind::Trapdoor, "trapdoor"}}};
constexpr std::array<std::pair<Plate::Weight, std::string_view>, 3> kPlateWeights = {
    {{Plate::Weight::Any, "any"}, {Plate::Weight::Creature, "creature"}, {Plate::Weight::Player, "player"}}};
constexpr std::array<std::pair<Mover::Crush, std::string_view>, 2> kMoverCrush = {
    {{Mover::Crush::Stop, "stop"}, {Mover::Crush::Damage, "damage"}}};

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

Result<WaterVolume> readWater(const Reader& r, const Json& v, std::string_view where,
                              const Transform& transform)
{
    const Json w = componentOf(v, "water");
    const std::string at = std::format("{}.components.water", where);
    if (!w.contains("halfExtents"))
    {
        return r.error(at, "needs 'halfExtents'");
    }
    WaterVolume water;
    auto half = r.vec3(w, "halfExtents", at, water.halfExtents);
    if (!half)
    {
        return half.error();
    }
    if (!(half.value().x > 0.0f && half.value().y > 0.0f && half.value().z > 0.0f))
    {
        return r.error(at, "'halfExtents' must be positive");
    }
    water.halfExtents = half.value();
    auto kind = readText(r, w, "kind", at, false);
    if (!kind)
    {
        return kind.error();
    }
    water.kind = std::move(kind).value();
    // The surface is the top of the box: it must stay level.
    const Quat& q = transform.rotation;
    if (std::abs(q.x) > 1e-4f || std::abs(q.z) > 1e-4f)
    {
        return r.error(where, "water may only be turned about Y (its top is the surface)");
    }
    if (glm::any(glm::greaterThan(glm::abs(transform.scale - Vec3(1.0f)), Vec3(1e-5f))))
    {
        return r.error(where, "water is not scaled - its size is 'halfExtents'");
    }
    return water;
}

/// `targets` (a list of vob names) and `action` of a trigger or relay (dungeon.md D1).
Result<void> readEventTargets(const Reader& r, const Json& object, std::string_view at,
                              std::vector<std::string>& targets, std::string& action)
{
    if (object.contains("targets"))
    {
        const Json& list = object["targets"];
        if (!list.is_array())
        {
            return r.error(at, "'targets' must be a list of vob names");
        }
        for (const Json& name : list)
        {
            if (!name.is_string() || name.get<std::string>().empty())
            {
                return r.error(at, "'targets' must be a list of vob names");
            }
            targets.push_back(name.get<std::string>());
        }
    }
    if (object.contains("action"))
    {
        const std::string value = object["action"].is_string() ? object["action"].get<std::string>() : "";
        if (!isEventAction(value))
        {
            std::string choices;
            for (const std::string_view a : kEventActions)
            {
                choices += (choices.empty() ? "'" : ", '") + std::string(a) + "'";
            }
            return r.error(at, std::format("'action' must be one of {}", choices));
        }
        action = value;
    }
    return {};
}

Result<Relay> readRelay(const Reader& r, const Json& v, std::string_view where)
{
    const Json c = componentOf(v, "relay");
    const std::string at = std::format("{}.components.relay", where);
    Relay relay;
    auto mode = readChoice(r, c, "mode", at, kRelayModes);
    auto delay = r.number(c, "delay", at, relay.delay);
    if (!mode || !delay)
    {
        return !mode ? mode.error() : delay.error();
    }
    relay.mode = mode.value();
    if (!(delay.value() >= 0.0f))
    {
        return r.error(at, "'delay' must be 0 or more seconds");
    }
    relay.delay = delay.value();
    if (c.contains("count"))
    {
        if (!c["count"].is_number_unsigned() || c["count"].get<u64>() == 0 ||
            c["count"].get<u64>() > 1'000'000)
        {
            return r.error(at + ".count", "must be a whole number from 1 to 1000000");
        }
        relay.count = static_cast<u32>(c["count"].get<u64>());
    }
    for (const auto& [key, flag] : {std::pair<const char*, bool*>{"once", &relay.once},
                                    std::pair<const char*, bool*>{"invert", &relay.invert}})
    {
        if (c.contains(key))
        {
            if (!c[key].is_boolean())
            {
                return r.error(std::format("{}.{}", at, key), "must be true or false");
            }
            *flag = c[key].get<bool>();
        }
    }
    if (auto events = readEventTargets(r, c, at, relay.targets, relay.action); !events)
    {
        return events.error();
    }
    return relay;
}

/// components.mover of a mesh or mob vob (dungeon D2).
Result<Mover> readMover(const Reader& r, const Json& v, std::string_view where)
{
    const Json c = componentOf(v, "mover");
    const std::string at = std::format("{}.components.mover", where);
    if (!c.is_object())
    {
        return r.error(at, "must be an object");
    }
    Mover mover;
    if (!c.contains("kind"))
    {
        return r.error(at, "needs 'kind' ('gate', 'secret_wall' or 'trapdoor')");
    }
    auto kind = readChoice(r, c, "kind", at, kMoverKinds);
    auto crush = readChoice(r, c, "crush", at, kMoverCrush);
    if (!kind || !crush)
    {
        return !kind ? kind.error() : crush.error();
    }
    mover.kind = kind.value();
    mover.crush = crush.value();
    auto seconds = r.number(c, "seconds", at, kMoverSeconds[static_cast<usize>(mover.kind)]);
    if (!seconds)
    {
        return seconds.error();
    }
    if (!(seconds.value() > 0.0f))
    {
        return r.error(at + ".seconds", "must be more than 0");
    }
    mover.seconds = seconds.value();
    if (c.contains("open"))
    {
        if (!c["open"].is_boolean())
        {
            return r.error(at + ".open", "must be true or false");
        }
        mover.open = c["open"].get<bool>();
    }
    for (const auto& [key, field] : {std::pair<const char*, std::optional<Vec3>*>{"offset", &mover.offset},
                                     std::pair<const char*, std::optional<Vec3>*>{"axis", &mover.axis}})
    {
        if (c.contains(key))
        {
            auto value = r.vec3(c, key, at, Vec3(0.0f));
            if (!value)
            {
                return value.error();
            }
            *field = value.value();
        }
    }
    if (mover.axis && glm::length(*mover.axis) < 1e-4f)
    {
        return r.error(at + ".axis", "must not be zero");
    }
    if (c.contains("angle"))
    {
        auto angle = r.number(c, "angle", at, 0.0f);
        if (!angle)
        {
            return angle.error();
        }
        mover.angle = angle.value();
    }
    for (const auto& [key, field] : {std::pair<const char*, std::string*>{"sound_move", &mover.soundMove},
                                     std::pair<const char*, std::string*>{"sound_stop", &mover.soundStop}})
    {
        auto text = readText(r, c, key, at, false);
        if (!text)
        {
            return text.error();
        }
        *field = std::move(text).value();
    }
    return mover;
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
    if (t.contains("changeWorld"))
    {
        const Json& c = t["changeWorld"];
        const std::string change = at + ".changeWorld";
        auto world = c.is_object() ? readText(r, c, "world", change, true)
                                   : Result<std::string>(r.error(change, "must be an object"));
        auto start =
            c.is_object() ? readText(r, c, "start", change, true) : Result<std::string>(std::string());
        if (!world || !start)
        {
            return !world ? world.error() : start.error();
        }
        if (trigger.filter != TriggerVolume::Filter::Player)
        {
            return r.error(change, "a level change reacts to the player only (filter 'player')");
        }
        trigger.changeWorld = std::move(world).value();
        trigger.changeStart = std::move(start).value();
    }
    if (t.contains("owner"))
    {
        auto owner = readText(r, t, "owner", at, true);
        if (!owner)
        {
            return owner.error();
        }
        trigger.owner = std::move(owner).value();
    }
    if (auto events = readEventTargets(r, t, at, trigger.targets, trigger.action); !events)
    {
        return events.error();
    }
    if (t.contains("plate"))
    {
        const Json& p = t["plate"];
        const std::string pat = at + ".plate";
        if (!p.is_object())
        {
            return r.error(pat, "must be an object");
        }
        Plate plate;
        auto weight = readChoice(r, p, "weight", pat, kPlateWeights);
        if (!weight)
        {
            return weight.error();
        }
        plate.weight = weight.value();
        if (p.contains("release_action"))
        {
            const std::string release =
                p["release_action"].is_string() ? p["release_action"].get<std::string>() : "";
            if (!isEventAction(release))
            {
                return r.error(pat, "'release_action' must be an action like 'action'");
            }
            plate.releaseAction = release;
        }
        trigger.plate = std::move(plate);
    }
    if (t.contains("target"))
    {
        // The formerly reserved single target (M4): a vob name counts as one of `targets`, an id is kept.
        if (t["target"].is_number_unsigned() && t["target"].get<u64>() > 0)
        {
            trigger.targetId = VobId{t["target"].get<u64>()};
        }
        else if (t["target"].is_string() && !t["target"].get<std::string>().empty())
        {
            const std::string name = t["target"].get<std::string>();
            if (std::find(trigger.targets.begin(), trigger.targets.end(), name) == trigger.targets.end())
            {
                trigger.targets.insert(trigger.targets.begin(), name);
            }
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
        // Normalized only when clearly off: written with six decimals a unit quaternion is off by ~1e-6, and
        // normalizing that would change the numbers written back (welt's generator, editor saves).
        const Quat read(q.value()[3], q.value()[0], q.value()[1], q.value()[2]);
        vob.transform.rotation = std::abs(glm::length(read) - 1.0f) > 1e-4f ? glm::normalize(read) : read;
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
        if (v.contains("components") && v["components"].contains("surface"))
        {
            const Json& surface = v["components"]["surface"];
            if (!surface.is_object() || !surface.contains("footstep") || !surface["footstep"].is_string() ||
                surface["footstep"].get<std::string>().empty())
            {
                return r.error(where,
                               "'components.surface' needs 'footstep' (a footstep material, e.g. \"wood\")");
            }
            vob.footstep = surface["footstep"].get<std::string>();
        }
        if (v.contains("components") && v["components"].contains("mover"))
        {
            auto mover = readMover(r, v, where);
            if (!mover)
            {
                return mover.error();
            }
            vob.mover = std::move(mover).value();
        }
    }
    else if (v.contains("components") && v["components"].is_object() && v["components"].contains("mover"))
    {
        return r.error(where, "'components.mover' belongs to a mesh or mob vob");
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
        if (light.contains("daylight"))
        {
            if (!light["daylight"].is_boolean())
            {
                return r.error(at + ".daylight", "must be true or false");
            }
            vob.light.daylight = light["daylight"].get<bool>();
        }
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
    if (vob.type == VobType::Relay)
    {
        auto relay = readRelay(r, v, where);
        if (!relay)
        {
            return relay.error();
        }
        vob.relay = std::move(relay).value();
    }
    if (vob.type == VobType::Water)
    {
        auto water = readWater(r, v, where, vob.transform);
        if (!water)
        {
            return water.error();
        }
        vob.water = std::move(water).value();
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
        const Json& mob = componentOf(v, "mob");
        if (mob.contains("open"))
        {
            if (!mob["open"].is_boolean())
            {
                return r.error(std::format("{}.components.mob.open", where), "must be true or false");
            }
            vob.mob.open = mob["open"].get<bool>();
        }
        const std::string mat = std::format("{}.components.mob", where);
        if (auto events = readEventTargets(r, mob, mat, vob.mob.targets, vob.mob.action); !events)
        {
            return events.error();
        }
        for (const auto& [key, flag] : {std::pair<const char*, bool*>{"once", &vob.mob.once},
                                        std::pair<const char*, bool*>{"hidden", &vob.mob.hidden}})
        {
            if (mob.contains(key))
            {
                if (!mob[key].is_boolean())
                {
                    return r.error(std::format("{}.{}", mat, key), "must be true or false");
                }
                *flag = mob[key].get<bool>();
            }
        }
    }
    if (vob.type == VobType::Item)
    {
        const Json item = componentOf(v, "item");
        const std::string at = std::format("{}.components.item", where);
        auto instance = readText(r, item, "instance", at, true);
        if (!instance)
        {
            return instance.error();
        }
        vob.item.instance = std::move(instance).value();
        if (item.contains("count"))
        {
            if (!item["count"].is_number_unsigned() || item["count"].get<u64>() == 0 ||
                item["count"].get<u64>() > 1'000'000)
            {
                return r.error(at + ".count", "must be a whole number from 1 to 1000000");
            }
            vob.item.count = static_cast<u32>(item["count"].get<u64>());
        }
        auto owner = readText(r, item, "owner", at, false);
        if (!owner)
        {
            return owner.error();
        }
        vob.item.owner = std::move(owner).value();
    }
    return vob;
}

/// The fewest decimals that read back to the same float (shortest fixed notation): -343.106 is stored as the
/// float -343.10598755 and written as "-343.106" again, -0.258819 as "-0.258819" - what welt's generator
/// wrote. A float that needs more than six decimals is noise from arithmetic (-0.0000000437 for a zero,
/// 0.70710677): it is rounded to 1e-5 (0.01 mm, quaternions well within float noise) first. Reading and
/// writing again stays identical. Python (welt): for d in 0..9 the first f"{f:.{d}f}" that gives the float32
/// f back; d > 6: round(f, 5) first.
double tidy(f32 value)
{
    const auto fixed = [](f32 v, char* buffer) -> std::string_view
    {
        const auto [end, ec] = std::to_chars(buffer, buffer + 64, v, std::chars_format::fixed);
        G7_ASSERT(ec == std::errc(), "to_chars of a float in fixed notation fits 64 characters");
        return {buffer, static_cast<usize>(end - buffer)};
    };
    char buffer[64];
    std::string_view text = fixed(value, buffer);
    const usize point = text.find('.');
    if (point != std::string_view::npos && text.size() - point - 1 > 6)
    {
        const double rounded = std::round(static_cast<double>(value) * 1e5) / 1e5;
        text = fixed(static_cast<f32>(rounded), buffer);
    }
    double result = 0.0;
    std::from_chars(text.data(), text.data() + text.size(), result);
    return result == 0.0 ? 0.0 : result; // no -0
}

/// A floating-point number with the fewest digits that read back to the same double (std::to_chars), laid out
/// like Python's repr and nlohmann: fixed for exponents -4..15 with at least one decimal ("4.0"), otherwise
/// "1e-05". nlohmann's Grisu2 is not always shortest (24.123169999999998 for 24.12317), so files written here
/// would differ from welt's generator for the same double.
std::string formatNumber(double value)
{
    if (!std::isfinite(value))
    {
        return "null"; // as nlohmann
    }
    if (value == 0.0)
    {
        return std::signbit(value) ? "-0.0" : "0.0";
    }
    char buffer[32];
    const auto [end, ec] =
        std::to_chars(buffer, buffer + sizeof(buffer), value, std::chars_format::scientific);
    G7_ASSERT(ec == std::errc(), "to_chars of a double fits 32 characters");
    const std::string_view text(buffer, static_cast<usize>(end - buffer));
    const usize e = text.find('e');
    const bool negative = text.front() == '-';
    std::string digits;
    for (const char c : text.substr(negative ? 1 : 0, e - (negative ? 1 : 0)))
    {
        if (c != '.')
        {
            digits += c;
        }
    }
    int exponent = 0;
    std::from_chars(text.data() + e + (text[e + 1] == '+' ? 2 : 1), text.data() + text.size(), exponent);
    std::string out = negative ? "-" : "";
    const int count = static_cast<int>(digits.size());
    if (exponent >= -4 && exponent < 16)
    {
        if (exponent < 0)
        {
            out += "0." + std::string(static_cast<usize>(-exponent - 1), '0') + digits;
        }
        else if (count <= exponent + 1)
        {
            out += digits + std::string(static_cast<usize>(exponent + 1 - count), '0') + ".0";
        }
        else
        {
            out += digits.substr(0, static_cast<usize>(exponent + 1)) + "." +
                   digits.substr(static_cast<usize>(exponent + 1));
        }
        return out;
    }
    out += digits.substr(0, 1);
    if (count > 1)
    {
        out += "." + digits.substr(1);
    }
    return out + std::format("e{}{:02}", exponent < 0 ? '-' : '+', std::abs(exponent));
}

/// Json::dump() (compact) with formatNumber for floating-point numbers.
std::string dumpJson(const Json& j)
{
    switch (j.type())
    {
    case Json::value_t::number_float:
        return formatNumber(j.get<double>());
    case Json::value_t::array:
    {
        std::string out = "[";
        for (usize i = 0; i < j.size(); ++i)
        {
            out += (i == 0 ? "" : ",") + dumpJson(j[i]);
        }
        return out + "]";
    }
    case Json::value_t::object:
    {
        std::string out = "{";
        bool first = true;
        for (const auto& [key, value] : j.items())
        {
            out += (first ? "" : ",") + Json(key).dump() + ":" + dumpJson(value);
            first = false;
        }
        return out + "}";
    }
    default:
        return j.dump();
    }
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

Result<WaynetPoint> readWaynetPoint(const Reader& r, const Json& p, const std::string& where)
{
    if (!p.is_object())
    {
        return r.error(where, "must be an object");
    }
    WaynetPoint point;
    auto name = readText(r, p, "name", where, true);
    if (!name)
    {
        return name.error();
    }
    point.name = std::move(name).value();
    if (!p.contains("pos"))
    {
        return r.error(where, "needs 'pos'");
    }
    auto pos = r.vec3(p, "pos", where, Vec3(0.0f));
    if (!pos)
    {
        return pos.error();
    }
    point.position = pos.value();
    if (p.contains("dir"))
    {
        auto dir = r.vec3(p, "dir", where, Vec3(0.0f));
        if (!dir)
        {
            return dir.error();
        }
        point.dir = dir.value();
    }
    if (p.contains("owner"))
    {
        if (!p["owner"].is_string() || p["owner"].get<std::string>() != "worldgen")
        {
            return r.error(where + ".owner", "must be \"worldgen\" (or left out)");
        }
        point.generated = true;
    }
    return point;
}

/// "zones" (world.md "Zonen"): indoor, music and ambient zones with a turned box, checked; other types kept
/// as they are.
Result<std::vector<Zone>> readZones(const Reader& r, const Json& z)
{
    if (!z.is_array())
    {
        return r.error("zones", "must be a list");
    }
    std::vector<Zone> zones;
    for (usize i = 0; i < z.size(); ++i)
    {
        const std::string where = std::format("zones[{}]", i);
        const Json& e = z[i];
        if (!e.is_object() || !e.contains("type") || !e["type"].is_string())
        {
            return r.error(where, "needs 'type'");
        }
        Zone zone;
        zone.type = e["type"].get<std::string>();
        if (e.contains("value") && e["value"].is_string())
        {
            zone.value = e["value"].get<std::string>();
        }
        if (zone.type != "indoor" && zone.type != "music" && zone.type != "ambient")
        {
            zone.json = e.dump();
            zones.push_back(std::move(zone));
            continue;
        }
        if (zone.value.empty())
        {
            return r.error(where, std::format("a{} {} zone needs 'value' ({})",
                                              zone.type == "indoor" ? "n" : "", zone.type,
                                              zone.type == "indoor"  ? "the room"
                                              : zone.type == "music" ? "the music theme"
                                                                     : "the ambience"));
        }
        if (!e.contains("box") || !e["box"].is_object())
        {
            return r.error(
                where, std::format("a{} {} zone needs 'box'", zone.type == "indoor" ? "n" : "", zone.type));
        }
        const Json& b = e["box"];
        const std::string at = where + ".box";
        if (!b.contains("center") || !b.contains("halfExtents"))
        {
            return r.error(at, "needs 'center' and 'halfExtents'");
        }
        auto center = r.vec3(b, "center", at, Vec3(0.0f));
        auto half = r.vec3(b, "halfExtents", at, Vec3(0.0f));
        auto yaw = r.number(b, "yaw", at, 0.0f);
        if (!center || !half || !yaw)
        {
            return !center ? center.error() : !half ? half.error() : yaw.error();
        }
        if (half.value().x <= 0.0f || half.value().y <= 0.0f || half.value().z <= 0.0f)
        {
            return r.error(at + ".halfExtents", "must be positive");
        }
        zone.box = ZoneBox{center.value(), half.value(), yaw.value()};
        zones.push_back(std::move(zone));
    }
    return zones;
}

Result<WaynetData> readWaynet(const Reader& r, const Json& w)
{
    if (!w.is_object())
    {
        return r.error("waynet", "must be an object");
    }
    WaynetData data;
    for (const auto& [key, list] :
         {std::pair{"points", &data.points}, std::pair{"freepoints", &data.freepoints}})
    {
        if (!w.contains(key))
        {
            continue;
        }
        if (!w[key].is_array())
        {
            return r.error(std::format("waynet.{}", key), "must be a list");
        }
        for (usize i = 0; i < w[key].size(); ++i)
        {
            auto point = readWaynetPoint(r, w[key][i], std::format("waynet.{}[{}]", key, i));
            if (!point)
            {
                return point.error();
            }
            list->push_back(std::move(point).value());
        }
    }
    if (w.contains("edges"))
    {
        if (!w["edges"].is_array())
        {
            return r.error("waynet.edges", "must be a list");
        }
        for (usize i = 0; i < w["edges"].size(); ++i)
        {
            const Json& e = w["edges"][i];
            const bool shape = e.is_array() && (e.size() == 2 || e.size() == 3) && e[0].is_string() &&
                               e[1].is_string() &&
                               (e.size() == 2 || (e[2].is_string() && e[2] == "worldgen"));
            if (!shape)
            {
                return r.error(std::format("waynet.edges[{}]", i),
                               "must be [\"WP_A\", \"WP_B\"] or [\"WP_A\", \"WP_B\", \"worldgen\"]");
            }
            data.edges.push_back({e[0].get<std::string>(), e[1].get<std::string>(), e.size() == 3});
        }
    }
    if (auto ok = normalizeWaynet(data, r.source); !ok)
    {
        return ok.error();
    }
    return data;
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
        auto waynet = readWaynet(r, root["waynet"]);
        if (!waynet)
        {
            return waynet.error();
        }
        world.waynet = std::move(waynet).value();
    }
    if (root.contains("zones"))
    {
        auto zones = readZones(r, root["zones"]);
        if (!zones)
        {
            return zones.error();
        }
        world.zones = std::move(zones).value();
    }
    if (root.contains("generator"))
    {
        // A hint for the editor only (world.md): kept as it is, and a broken or stale entry never fails
        // loading - the generator decides about its vobs itself.
        world.generatorJson = root["generator"].dump();
        const Json& g = root["generator"];
        const Json owned = g.is_object() && g.contains("owned") ? g["owned"] : Json::array();
        bool readable = owned.is_array();
        for (const Json& entry : readable ? owned : Json::array())
        {
            if (entry.is_number_unsigned())
            {
                world.generatorOwned.emplace_back(entry.get<u64>(), entry.get<u64>());
            }
            else if (entry.is_array() && entry.size() == 2 && entry[0].is_number_unsigned() &&
                     entry[1].is_number_unsigned() && entry[0].get<u64>() <= entry[1].get<u64>())
            {
                world.generatorOwned.emplace_back(entry[0].get<u64>(), entry[1].get<u64>());
            }
            else
            {
                readable = false;
            }
        }
        if (!readable)
        {
            G7_LOG_WARN("world", "{}: 'generator.owned' is not a list of ids and [from, to] ranges - ignored",
                        source);
            world.generatorOwned.clear();
        }
    }
    return world;
}

bool zoneContains(const ZoneBox& box, const Vec3& point) noexcept
{
    // Into the box's own axes: local +X = (cos yaw, 0, -sin yaw), local +Z = (sin yaw, 0, cos yaw).
    const Vec3 d = point - box.center;
    const f32 yaw = glm::radians(box.yawDegrees);
    const f32 c = std::cos(yaw);
    const f32 s = std::sin(yaw);
    const Vec3 local(d.x * c - d.z * s, d.y, d.x * s + d.z * c);
    return std::abs(local.x) <= box.halfExtents.x && std::abs(local.y) <= box.halfExtents.y &&
           std::abs(local.z) <= box.halfExtents.z;
}

const Zone* smallestZone(std::span<const Zone> zones, std::string_view type, const Vec3& point,
                         f32* volume) noexcept
{
    const Zone* best = nullptr;
    f32 bestVolume = 0.0f;
    for (const Zone& z : zones)
    {
        if (z.type != type || !z.box || !zoneContains(*z.box, point))
        {
            continue;
        }
        const f32 v = z.box->halfExtents.x * z.box->halfExtents.y * z.box->halfExtents.z;
        if (best == nullptr || v < bestVolume)
        {
            best = &z;
            bestVolume = v;
        }
    }
    if (volume != nullptr)
    {
        *volume = bestVolume;
    }
    return best;
}

Result<void> checkZone(const Zone& zone, std::string_view where)
{
    if (zone.type.empty())
    {
        return Error{std::format("{}: needs 'type'", where)};
    }
    if (zone.type != "indoor" && zone.type != "music" && zone.type != "ambient")
    {
        return {}; // kept as it is
    }
    if (zone.value.empty())
    {
        return Error{std::format("{}: a {} zone needs 'value'", where, zone.type)};
    }
    if (!zone.box)
    {
        return Error{std::format("{}: a {} zone needs 'box'", where, zone.type)};
    }
    const Vec3& h = zone.box->halfExtents;
    if (!(h.x > 0.0f && h.y > 0.0f && h.z > 0.0f) || !std::isfinite(h.x + h.y + h.z) ||
        !std::isfinite(zone.box->center.x + zone.box->center.y + zone.box->center.z + zone.box->yawDegrees))
    {
        return Error{std::format("{}.box.halfExtents: must be positive", where)};
    }
    return {};
}

bool isGenerated(const WorldFile& world, VobId id) noexcept
{
    return std::any_of(world.generatorOwned.begin(), world.generatorOwned.end(), [&](const auto& range)
                       { return id.value >= range.first && id.value <= range.second; });
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

namespace
{
/// One vob as written into the file (writeWorldFile, writeVobJson).
Json vobJson(const WorldFileVob& vob)
{
    Json v = Json::object();
    v["id"] = vob.id.value;
    v["type"] = vobTypeName(vob.type);
    v["name"] = vob.name;
    if (vob.parent.valid())
    {
        v["parent"] = vob.parent.value;
    }
    const Transform& t = vob.transform;
    v["pos"] = numbers({t.position.x, t.position.y, t.position.z});
    v["rot"] = numbers({t.rotation.x, t.rotation.y, t.rotation.z, t.rotation.w});
    if (t.scale != Vec3(1.0f))
    {
        v["scale"] = numbers({t.scale.x, t.scale.y, t.scale.z});
    }
    if (vob.type == VobType::Mesh || vob.type == VobType::Mob)
    {
        v["mesh"] = vob.mesh;
        if (vob.type == VobType::Mesh && vob.category == VobCategory::Gameplay)
        {
            v["category"] = "gameplay"; // deco is the default and not written
        }
    }
    if (vob.type == VobType::Light)
    {
        const LightSource& l = vob.light;
        Json light = Json{{"color", numbers({l.color.r, l.color.g, l.color.b})},
                          {"range", tidy(l.range)},
                          {"intensity", tidy(l.intensity)}};
        if (l.daylight)
        {
            light["daylight"] = true; // after intensity, only when true (contract with welt)
        }
        light["flicker"] = tidy(l.flicker);
        v["components"]["light"] = std::move(light);
    }
    if (vob.type == VobType::Sound)
    {
        const SoundEmitter& s = vob.sound;
        v["components"]["sound"] = Json{{"sound", s.sound},
                                        {"range", tidy(s.range)},
                                        {"volume", tidy(s.volume)},
                                        {"mode", nameOf(kSoundModes, s.mode)},
                                        {"delay", numbers({s.delay.x, s.delay.y})}};
    }
    if (vob.type == VobType::Trigger)
    {
        const TriggerVolume& tv = vob.trigger;
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
        if (!tv.targets.empty())
        {
            trigger["targets"] = tv.targets; // with its action, only when there are targets
            trigger["action"] = tv.action;
        }
        if (tv.plate)
        {
            Json plate = Json{{"weight", nameOf(kPlateWeights, tv.plate->weight)}};
            if (!tv.plate->releaseAction.empty())
            {
                plate["release_action"] = tv.plate->releaseAction;
            }
            trigger["plate"] = std::move(plate);
        }
        if (!tv.changeWorld.empty())
        {
            trigger["changeWorld"] = Json{{"world", tv.changeWorld}, {"start", tv.changeStart}};
        }
        if (!tv.owner.empty())
        {
            trigger["owner"] = tv.owner;
        }
        v["components"]["trigger"] = std::move(trigger);
    }
    if (vob.type == VobType::Mob)
    {
        v["components"]["mob"] = Json{{"definition", vob.mob.definition}};
        if (vob.mob.open)
        {
            v["components"]["mob"]["open"] = true; // after definition, only when true (welt: byte-equal)
        }
        if (!vob.mob.targets.empty())
        {
            v["components"]["mob"]["targets"] = vob.mob.targets; // switches (dungeon D3)
            v["components"]["mob"]["action"] = vob.mob.action;
        }
        if (vob.mob.once)
        {
            v["components"]["mob"]["once"] = true;
        }
        if (vob.mob.hidden)
        {
            v["components"]["mob"]["hidden"] = true;
        }
    }
    if (vob.type == VobType::Item)
    {
        Json item = Json{{"instance", vob.item.instance}};
        if (vob.item.count != 1)
        {
            item["count"] = vob.item.count;
        }
        if (!vob.item.owner.empty())
        {
            item["owner"] = vob.item.owner;
        }
        v["components"]["item"] = std::move(item);
    }
    if ((vob.type == VobType::Mesh || vob.type == VobType::Mob) && !vob.footstep.empty())
    {
        v["components"]["surface"] = Json{{"footstep", vob.footstep}}; // only when set (byte-equal)
    }
    if ((vob.type == VobType::Mesh || vob.type == VobType::Mob) && vob.mover)
    {
        // kind, open, seconds, crush always; the motion and sounds only when given.
        const Mover& m = *vob.mover;
        Json mover =
            Json{{"kind", nameOf(kMoverKinds, m.kind)}, {"open", m.open}, {"seconds", tidy(m.seconds)}};
        if (m.offset)
        {
            mover["offset"] = numbers({m.offset->x, m.offset->y, m.offset->z});
        }
        if (m.axis)
        {
            mover["axis"] = numbers({m.axis->x, m.axis->y, m.axis->z});
        }
        if (m.angle)
        {
            mover["angle"] = tidy(*m.angle);
        }
        if (!m.soundMove.empty())
        {
            mover["sound_move"] = m.soundMove;
        }
        if (!m.soundStop.empty())
        {
            mover["sound_stop"] = m.soundStop;
        }
        mover["crush"] = nameOf(kMoverCrush, m.crush);
        v["components"]["mover"] = std::move(mover);
    }
    if (vob.type == VobType::Relay)
    {
        const Relay& r = vob.relay;
        Json relay = Json{{"mode", nameOf(kRelayModes, r.mode)}};
        if (r.mode == Relay::Mode::Count)
        {
            relay["count"] = r.count;
        }
        relay["delay"] = tidy(r.delay);
        relay["once"] = r.once;
        relay["invert"] = r.invert;
        relay["targets"] = r.targets;
        relay["action"] = r.action;
        v["components"]["relay"] = std::move(relay);
    }
    if (vob.type == VobType::Water)
    {
        Json water =
            Json{{"halfExtents",
                  numbers({vob.water.halfExtents.x, vob.water.halfExtents.y, vob.water.halfExtents.z})}};
        if (!vob.water.kind.empty())
        {
            water["kind"] = vob.water.kind;
        }
        v["components"]["water"] = std::move(water);
    }
    return v;
}
} // namespace

std::string writeWorldFile(const WorldFile& world)
{
    Json root = Json::object();
    root["version"] = kWorldFileVersion;
    root["name"] = world.name;
    root["nextVobId"] = world.nextVobId;
    if (!world.generatorJson.empty())
    {
        root["generator"] = Json::parse(world.generatorJson, nullptr, false);
    }
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
        vobs.push_back(vobJson(*vob));
    }
    // Layout for diffs: the header keys one per line, then one vob per line, so changing a vob
    // changes exactly one line. Waynet and zones follow as compact values.
    std::string out = "{\n";
    for (const auto& [key, value] : root.items())
    {
        out += "  \"" + key + "\": " + dumpJson(value) + ",\n";
    }
    out += "  \"vobs\": [";
    for (usize i = 0; i < vobs.size(); ++i)
    {
        out += (i == 0 ? "\n    " : ",\n    ") + dumpJson(vobs[i]);
    }
    out += vobs.empty() ? "]" : "\n  ]";
    if (world.waynet)
    {
        // One point, freepoint or edge per line, sorted (normalizeWaynet): moving a point changes one line.
        const auto point = [](const WaynetPoint& p)
        {
            Json j = Json{{"name", p.name}, {"pos", numbers({p.position.x, p.position.y, p.position.z})}};
            if (p.dir)
            {
                j["dir"] = numbers({p.dir->x, p.dir->y, p.dir->z});
            }
            if (p.generated)
            {
                j["owner"] = "worldgen";
            }
            return dumpJson(j);
        };
        const auto list = [](const std::vector<std::string>& lines)
        {
            std::string text = "[";
            for (usize i = 0; i < lines.size(); ++i)
            {
                text += (i == 0 ? "\n      " : ",\n      ") + lines[i];
            }
            return text + (lines.empty() ? "]" : "\n    ]");
        };
        std::vector<std::string> points;
        std::vector<std::string> freepoints;
        std::vector<std::string> edges;
        for (const WaynetPoint& p : world.waynet->points)
        {
            points.push_back(point(p));
        }
        for (const WaynetPoint& p : world.waynet->freepoints)
        {
            freepoints.push_back(point(p));
        }
        for (const WaynetEdge& e : world.waynet->edges)
        {
            Json j = Json::array({e.a, e.b});
            if (e.generated)
            {
                j.push_back("worldgen");
            }
            edges.push_back(dumpJson(j));
        }
        out += ",\n  \"waynet\": {\n    \"points\": " + list(points) + ",\n    \"edges\": " + list(edges) +
               ",\n    \"freepoints\": " + list(freepoints) + "\n  }";
    }
    if (!world.zones.empty())
    {
        // One zone per line, sorted by value; rooms of several boxes by the box centre (x, then z).
        std::vector<const Zone*> zones;
        for (const Zone& z : world.zones)
        {
            zones.push_back(&z);
        }
        std::stable_sort(zones.begin(), zones.end(),
                         [](const Zone* a, const Zone* b)
                         {
                             if (a->value != b->value)
                             {
                                 return a->value < b->value;
                             }
                             if (!a->box || !b->box)
                             {
                                 return false;
                             }
                             return a->box->center.x != b->box->center.x
                                        ? a->box->center.x < b->box->center.x
                                        : a->box->center.z < b->box->center.z;
                         });
        out += ",\n  \"zones\": [";
        for (usize i = 0; i < zones.size(); ++i)
        {
            const Zone& z = *zones[i];
            std::string line;
            if (z.box)
            {
                const Vec3& c = z.box->center;
                const Vec3& h = z.box->halfExtents;
                Json box = Json::object();
                box["center"] = numbers({c.x, c.y, c.z});
                box["halfExtents"] = numbers({h.x, h.y, h.z});
                box["yaw"] = numbers({z.box->yawDegrees})[0];
                line = "{\"type\":" + Json(z.type).dump() + ",\"value\":" + Json(z.value).dump() +
                       ",\"box\":{\"center\":" + dumpJson(box["center"]) +
                       ",\"halfExtents\":" + dumpJson(box["halfExtents"]) +
                       ",\"yaw\":" + dumpJson(box["yaw"]) + "}}";
            }
            else
            {
                line = dumpJson(Json::parse(z.json, nullptr, false));
            }
            out += (i == 0 ? "\n    " : ",\n    ") + line;
        }
        out += "\n  ]";
    }
    out += "\n}\n";
    return out;
}

std::string writeVobJson(const WorldFileVob& vob)
{
    return dumpJson(vobJson(vob));
}

Result<WorldFileVob> parseVobJson(std::string_view json, std::string_view source)
{
    const Json v = Json::parse(json.begin(), json.end(), nullptr, false);
    const Reader r{source};
    if (v.is_discarded())
    {
        return r.error("vob", "not valid JSON");
    }
    return readVob(r, v, "vob");
}
} // namespace g7::world
