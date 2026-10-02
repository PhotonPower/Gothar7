#include <g7/asset/Vfs.hpp>
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

constexpr std::array<std::pair<VobType, std::string_view>, 3> kVobTypes = {
    {{VobType::Empty, "empty"}, {VobType::Mesh, "mesh"}, {VobType::Light, "light"}}};

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

    if (vob.type == VobType::Mesh)
    {
        if (!v.contains("mesh") || !v["mesh"].is_string() || v["mesh"].get<std::string>().empty())
        {
            return r.error(where, "a mesh vob needs 'mesh' (VFS path)");
        }
        vob.mesh = v["mesh"].get<std::string>();
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
    return vob;
}

/// Numbers rounded to 1e-5 (0.01 mm, quaternions well within float noise): float rounding like
/// -4.37e-08 for a zero would make every save differ; reading and writing again stays identical.
double tidy(f32 value)
{
    const double rounded = std::round(static_cast<double>(value) * 1e5) / 1e5;
    return rounded == 0.0 ? 0.0 : rounded; // no -0
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
    if (root.contains("vobs"))
    {
        if (!root["vobs"].is_array())
        {
            return r.error("vobs", "must be a list");
        }
        std::unordered_set<u64> ids;
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
        if (vob->type == VobType::Mesh)
        {
            v["mesh"] = vob->mesh;
        }
        if (vob->type == VobType::Light)
        {
            const LightSource& l = vob->light;
            v["components"]["light"] = Json{{"color", numbers({l.color.r, l.color.g, l.color.b})},
                                            {"range", tidy(l.range)},
                                            {"intensity", tidy(l.intensity)},
                                            {"flicker", tidy(l.flicker)}};
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
        if (vob->type == VobType::Mesh)
        {
            scene.set<MeshRef>(e.value(), {vob->mesh});
        }
        else if (vob->type == VobType::Light)
        {
            scene.set<LightSource>(e.value(), vob->light);
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
            if (const MeshRef* mesh = scene.get<MeshRef>(e))
            {
                out.type = VobType::Mesh;
                out.mesh = mesh->path;
            }
            else if (const LightSource* light = scene.get<LightSource>(e))
            {
                out.type = VobType::Light;
                out.light = *light;
            }
            world.vobs.push_back(std::move(out));
        });
    return world;
}
} // namespace g7::world
