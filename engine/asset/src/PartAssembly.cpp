// `asset.extras.gothar` of figure parts (characters-pipeline.md §6.2, format v1): read from the glTF JSON
// with nlohmann-json (private, ADR 0017) - fastgltf hands extras over only as simdjson objects.

#include "PartAssembly.hpp"

#include <nlohmann/json.hpp>

#include <cstring>
#include <format>

namespace g7::asset
{
namespace
{
using Json = nlohmann::json;

/// The JSON text of a .gltf (all of it) or .glb (chunk 0).
std::string_view jsonText(std::span<const u8> bytes)
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

bool unsignedAt(const Json& array, usize index, u32& out)
{
    if (!array.is_array() || index >= array.size() || !array[index].is_number_unsigned())
    {
        return false;
    }
    out = array[index].get<u32>();
    return true;
}
} // namespace

std::map<std::string, std::vector<std::string>> readMorphNames(std::span<const u8> bytes)
{
    std::map<std::string, std::vector<std::string>> out;
    const std::string_view text = jsonText(bytes);
    const Json doc = Json::parse(text.begin(), text.end(), nullptr, false);
    if (doc.is_discarded() || !doc.contains("nodes") || !doc.contains("meshes"))
    {
        return out;
    }
    const Json& meshes = doc["meshes"];
    for (const Json& node : doc["nodes"])
    {
        u32 mesh = 0;
        if (!node.is_object() || !node.contains("name") || !node.contains("mesh") ||
            !unsignedAt(Json::array({node["mesh"]}), 0, mesh) || mesh >= meshes.size())
        {
            continue;
        }
        const Json& extras = meshes[mesh].value("extras", Json::object());
        if (!extras.is_object() || !extras.contains("targetNames") || !extras["targetNames"].is_array())
        {
            continue;
        }
        std::vector<std::string> names;
        for (const Json& n : extras["targetNames"])
        {
            names.push_back(n.is_string() ? n.get<std::string>() : std::string());
        }
        out[node["name"].get<std::string>()] = std::move(names);
    }
    return out;
}

Result<SkinnedModelData::Assembly> readPartAssembly(std::span<const u8> bytes, std::string_view debugName)
{
    SkinnedModelData::Assembly out;
    const std::string_view text = jsonText(bytes);
    const Json doc = Json::parse(text.begin(), text.end(), nullptr, false);
    if (doc.is_discarded() || !doc.is_object())
    {
        return out; // fastgltf has judged the file already; without readable JSON there is no assembly data
    }
    const auto asset = doc.find("asset");
    if (asset == doc.end() || !asset->is_object())
    {
        return out;
    }
    const auto extras = asset->find("extras");
    if (extras == asset->end() || !extras->is_object())
    {
        return out;
    }
    const auto gothar = extras->find("gothar");
    if (gothar == extras->end() || !gothar->is_object() || !gothar->contains("part"))
    {
        return out; // figures carry {figure, inputs} here, not part data
    }
    const auto bad = [&](std::string_view what)
    { return Error{std::format("{}: asset.extras.gothar: {}", debugName, what)}; };
    const Json& g = *gothar;
    if (!g.contains("version") || !g["version"].is_number_unsigned() || g["version"].get<u32>() != 1)
    {
        return bad("needs version 1");
    }
    out.version = 1;
    if (!g["part"].is_string())
    {
        return bad("'part' must be a string");
    }
    out.part = g["part"].get<std::string>();

    if (const auto neck = g.find("neck"); neck != g.end())
    {
        if (!neck->is_object())
        {
            return bad("'neck' must map LOD nodes to rings");
        }
        for (const auto& [node, ring] : neck->items())
        {
            if (!ring.is_array())
            {
                return bad(std::format("neck.{} must be a list of ring points", node));
            }
            auto& points = out.neck[node];
            for (const Json& point : ring)
            {
                std::vector<std::array<u32, 2>> vertices;
                for (usize i = 0; point.is_array() && i < point.size(); ++i)
                {
                    std::array<u32, 2> v{};
                    if (!unsignedAt(point[i], 0, v[0]) || !unsignedAt(point[i], 1, v[1]))
                    {
                        return bad(std::format("neck.{}: ring points list [primitive, vertex] pairs", node));
                    }
                    vertices.push_back(v);
                }
                if (vertices.empty())
                {
                    return bad(std::format("neck.{}: empty ring point", node));
                }
                points.push_back(std::move(vertices));
            }
        }
    }
    if (const auto falloff = g.find("falloff"); falloff != g.end() && falloff->is_object())
    {
        for (const auto& [node, entries] : falloff->items())
        {
            auto& list = out.falloff[node];
            for (usize i = 0; entries.is_array() && i < entries.size(); ++i)
            {
                const Json& e = entries[i];
                SkinnedModelData::Assembly::Falloff f;
                if (!unsignedAt(e, 0, f.primitive) || !unsignedAt(e, 1, f.vertex) ||
                    !unsignedAt(e, 2, f.ringPoint) || e.size() < 4 || !e[3].is_number())
                {
                    return bad(
                        std::format("falloff.{}: entries are [primitive, vertex, ring point, weight]", node));
                }
                f.weight = e[3].get<f64>();
                list.push_back(f);
            }
        }
    }
    if (const auto covers = g.find("covers"); covers != g.end())
    {
        if (!covers->is_object() || !covers->contains("body") || !(*covers)["body"].is_string())
        {
            return bad("'covers' needs 'body'");
        }
        out.coversBody = (*covers)["body"].get<std::string>();
        if (const auto lods = covers->find("lods"); lods != covers->end() && lods->is_object())
        {
            for (const auto& [node, ranges] : lods->items())
            {
                auto& list = out.covers[node];
                for (usize i = 0; ranges.is_array() && i < ranges.size(); ++i)
                {
                    std::array<u32, 3> r{};
                    if (!unsignedAt(ranges[i], 0, r[0]) || !unsignedAt(ranges[i], 1, r[1]) ||
                        !unsignedAt(ranges[i], 2, r[2]) || r[2] < r[1])
                    {
                        return bad(std::format("covers.lods.{}: ranges are [primitive, first, end)", node));
                    }
                    list.push_back(r);
                }
            }
        }
    }
    if (const auto hides = g.find("hides"); hides != g.end())
    {
        for (usize i = 0; hides->is_array() && i < hides->size(); ++i)
        {
            if (!(*hides)[i].is_string())
            {
                return bad("'hides' lists role names");
            }
            out.hides.push_back((*hides)[i].get<std::string>());
        }
    }
    return out;
}
} // namespace g7::asset
