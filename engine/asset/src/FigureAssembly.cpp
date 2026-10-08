#include <g7/asset/FigureAssembly.hpp>
#include <g7/core/Config.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <format>
#include <map>

namespace g7::asset
{
namespace
{
constexpr std::array<std::string_view, 4> kRoles = {"body", "head", "hair", "beard"};
constexpr std::array<std::string_view, 2> kHideable = {"hair", "beard"};
constexpr std::string_view kClothPrefix = "cloth_";
constexpr f64 kNeckLiftMax = 0.006; // metres the neck seam may move up or down when snapping (assemble.py)

Error fail(std::string_view source, std::string_view message)
{
    return Error{std::format("{}: {}", source, message)};
}

bool relativeGlb(std::string_view path)
{
    return path.ends_with(".glb") && !path.starts_with('/') && !path.starts_with('\\') &&
           !(path.size() > 1 && path[1] == ':');
}

Vec3 srgbToLinear(std::string_view hex)
{
    const auto channel = [&](usize at)
    {
        const f64 s = static_cast<f64>(std::stoi(std::string(hex.substr(at, 2)), nullptr, 16)) / 255.0;
        const f64 linear = s <= 0.04045 ? s / 12.92 : std::pow((s + 0.055) / 1.055, 2.4);
        return static_cast<f32>(std::round(linear * 1e6) / 1e6); // as assemble.py writes it
    };
    return Vec3(channel(1), channel(3), channel(5));
}

/// "skin.001" -> "skin" (Blender's duplicate suffix).
std::string baseName(std::string_view name)
{
    if (name.size() > 4 && name[name.size() - 4] == '.' &&
        std::all_of(name.end() - 3, name.end(),
                    [](char c) { return std::isdigit(static_cast<unsigned char>(c)); }))
    {
        return std::string(name.substr(0, name.size() - 4));
    }
    return std::string(name);
}

/// "a/b/c.glb" + "../../t/x.jpg" -> "t/x.jpg" (relative to the VFS root).
std::string resolveUri(std::string_view file, std::string_view uri)
{
    std::vector<std::string> parts;
    const auto push = [&](std::string_view path)
    {
        usize start = 0;
        while (start <= path.size())
        {
            const usize end = std::min(path.find_first_of("/\\", start), path.size());
            const std::string_view piece = path.substr(start, end - start);
            if (piece == "..")
            {
                if (!parts.empty())
                {
                    parts.pop_back();
                }
            }
            else if (!piece.empty() && piece != ".")
            {
                parts.emplace_back(piece);
            }
            start = end + 1;
        }
    };
    const usize slash = file.find_last_of("/\\");
    push(slash == std::string_view::npos ? std::string_view() : file.substr(0, slash));
    push(uri);
    std::string out;
    for (const std::string& p : parts)
    {
        out += (out.empty() ? "" : "/") + p;
    }
    return out;
}

const FigurePart* partOf(std::span<const FigurePart> parts, std::string_view role)
{
    const auto it =
        std::find_if(parts.begin(), parts.end(), [&](const FigurePart& p) { return p.role == role; });
    return it == parts.end() ? nullptr : &*it;
}

Result<u32> vertexIndex(const SkinnedPartData& part, u32 primitive, u32 vertex, std::string_view what)
{
    if (primitive >= part.primitives.size() || vertex >= part.primitives[primitive].vertexCount)
    {
        return Error{std::format("{}: [{}, {}] is not a vertex of {}", what, primitive, vertex, part.node)};
    }
    return part.primitives[primitive].firstVertex + vertex;
}

/// New body positions: its neck ring onto the head's ring (paired by cyclic shift and direction with the
/// least total distance), the falloff vertices following with their weight. In double like assemble.py.
Result<std::vector<Vec3>> snapNeck(const SkinnedPartData& body, const SkinnedModelData::Assembly& bodyData,
                                   const SkinnedPartData& head, const SkinnedModelData::Assembly& headData)
{
    const auto ringIt = bodyData.neck.find(body.node);
    const auto headIt = headData.neck.find(head.node);
    if (ringIt == bodyData.neck.end() || headIt == headData.neck.end() || ringIt->second.empty())
    {
        return Error{std::format("no neck ring for {} / {}", body.node, head.node)};
    }
    const auto& ring = ringIt->second;
    const auto& headRing = headIt->second;
    if (ring.size() != headRing.size())
    {
        return Error{std::format("neck rings differ: {} {} vs {} {} points", body.node, ring.size(),
                                 head.node, headRing.size())};
    }
    const usize n = ring.size();
    std::vector<glm::dvec3> bodyPoints(n);
    std::vector<glm::dvec3> headPoints(n);
    for (usize k = 0; k < n; ++k)
    {
        auto b = vertexIndex(body, ring[k][0][0], ring[k][0][1], "neck");
        auto h = vertexIndex(head, headRing[k][0][0], headRing[k][0][1], "neck");
        if (!b || !h)
        {
            return !b ? b.error() : h.error();
        }
        bodyPoints[k] = glm::dvec3(body.vertices[b.value()].position);
        headPoints[k] = glm::dvec3(head.vertices[h.value()].position);
    }
    std::vector<usize> best;
    f64 bestCost = 0.0;
    for (const bool reversed : {false, true})
    {
        for (usize shift = 0; shift < n; ++shift)
        {
            std::vector<usize> pairing(n);
            f64 cost = 0.0;
            for (usize k = 0; k < n; ++k)
            {
                const usize at = (k + shift) % n;
                pairing[k] = reversed ? n - 1 - at : at;
                cost += glm::length(bodyPoints[k] - headPoints[pairing[k]]);
            }
            if (best.empty() || cost < bestCost)
            {
                best = std::move(pairing);
                bestCost = cost;
            }
        }
    }
    std::vector<glm::dvec3> delta(n);
    f64 lift = 0.0;
    for (usize k = 0; k < n; ++k)
    {
        delta[k] = headPoints[best[k]] - bodyPoints[k];
        lift += delta[k].y / static_cast<f64>(n);
    }
    if (std::abs(lift) > kNeckLiftMax)
    {
        return Error{std::format("{} sits {:+.0f} mm above the neck of {} (max {:.0f} mm): rebuild the head",
                                 head.node, lift * 1000.0, body.node, kNeckLiftMax * 1000.0)};
    }
    std::vector<glm::dvec3> positions(body.vertices.size());
    for (usize i = 0; i < positions.size(); ++i)
    {
        positions[i] = glm::dvec3(body.vertices[i].position);
    }
    if (const auto falloff = bodyData.falloff.find(body.node); falloff != bodyData.falloff.end())
    {
        for (const SkinnedModelData::Assembly::Falloff& f : falloff->second)
        {
            auto v = vertexIndex(body, f.primitive, f.vertex, "falloff");
            if (!v || f.ringPoint >= n)
            {
                return v ? Error{std::format("falloff of {}: ring point {} out of range", body.node,
                                             f.ringPoint)}
                         : v.error();
            }
            positions[v.value()] += delta[f.ringPoint] * f.weight;
        }
    }
    for (usize k = 0; k < n; ++k)
    {
        for (const auto& [primitive, vertex] : ring[k])
        {
            auto v = vertexIndex(body, primitive, vertex, "neck");
            if (!v)
            {
                return v.error();
            }
            positions[v.value()] = headPoints[best[k]];
        }
    }
    std::vector<Vec3> out(positions.size());
    std::transform(positions.begin(), positions.end(), out.begin(),
                   [](const glm::dvec3& p) { return Vec3(p); });
    return out;
}
} // namespace

std::string clothRole(std::string_view path)
{
    std::string_view stem =
        path.substr(path.find_last_of("/\\") == std::string_view::npos ? 0 : path.find_last_of("/\\") + 1);
    stem = stem.substr(0, stem.rfind('.'));
    std::string out;
    bool gap = false;
    for (const char c : stem)
    {
        const char lower = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if ((lower >= 'a' && lower <= 'z') || (lower >= '0' && lower <= '9'))
        {
            if (gap && !out.empty())
            {
                out += '_';
            }
            out += lower;
            gap = false;
        }
        else
        {
            gap = true;
        }
    }
    return std::string(kClothPrefix) + out;
}

Result<FigureManifest> FigureManifest::parse(std::string_view toml, std::string_view source)
{
    auto parsed = Config::parse(toml, source);
    if (!parsed)
    {
        return parsed.error();
    }
    const Config& c = parsed.value();
    if (c.find<i64>("version") != 1)
    {
        return fail(source, "needs 'version = 1'");
    }
    FigureManifest manifest;
    for (const std::string& key : c.keys("parts"))
    {
        if (key != "cloth" && std::find(kRoles.begin(), kRoles.end(), key) == kRoles.end())
        {
            return fail(source, std::format("unknown part role '{}'", key));
        }
    }
    for (const std::string_view role : kRoles)
    {
        if (const auto path = c.find<std::string>("parts." + std::string(role)))
        {
            if (!relativeGlb(*path))
            {
                return fail(source, std::format("part '{}': expected a relative .glb path", role));
            }
            manifest.parts.push_back({std::string(role), *path});
        }
    }
    for (const std::string& path : c.get<std::vector<std::string>>("parts.cloth", {}))
    {
        if (!relativeGlb(path))
        {
            return fail(source, std::format("garment '{}': expected a relative .glb path", path));
        }
        if (manifest.find(clothRole(path)) != nullptr)
        {
            return fail(source, std::format("garment '{}' listed twice", path));
        }
        manifest.parts.push_back({clothRole(path), path});
    }
    if (manifest.find("body") == nullptr || manifest.find("head") == nullptr)
    {
        return fail(source, "needs parts 'body' and 'head'");
    }
    for (const std::string& material : c.keys("palette"))
    {
        const auto colour = c.find<std::string>("palette." + material);
        const bool hex = colour && colour->size() == 7 && (*colour)[0] == '#' &&
                         std::all_of(colour->begin() + 1, colour->end(), [](char ch)
                                     { return std::isxdigit(static_cast<unsigned char>(ch)) != 0; });
        if (!hex)
        {
            return fail(source, std::format("palette '{}': expected '#rrggbb'", material));
        }
        manifest.palette.emplace_back(material, srgbToLinear(*colour));
    }
    manifest.animVariant = c.get<std::string>("anim.variant", "");
    if (manifest.animVariant.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789_") != std::string::npos)
    {
        return fail(source, std::format("[anim] variant '{}': lower-case letters, digits and _",
                                        manifest.animVariant));
    }
    return manifest;
}

const FigureManifest::Part* FigureManifest::find(std::string_view role) const noexcept
{
    const auto it = std::find_if(parts.begin(), parts.end(), [&](const Part& p) { return p.role == role; });
    return it == parts.end() ? nullptr : &*it;
}

Result<void> FigureManifest::setPart(std::string_view role, std::string_view path)
{
    if (std::find(kRoles.begin(), kRoles.end(), role) == kRoles.end())
    {
        return Error{std::format("unknown part role '{}' (garments: setCloth)", role)};
    }
    const auto it = std::find_if(parts.begin(), parts.end(), [&](const Part& p) { return p.role == role; });
    if (path.empty())
    {
        if (role == "body" || role == "head")
        {
            return Error{std::format("a figure needs a {}", role)};
        }
        if (it != parts.end())
        {
            parts.erase(it);
        }
        return {};
    }
    if (!relativeGlb(path))
    {
        return Error{std::format("part '{}': expected a relative .glb path", role)};
    }
    if (it != parts.end())
    {
        it->path = std::string(path);
        return {};
    }
    // Keep the order body, head, hair, beard, garments.
    const auto rank = [](std::string_view r)
    {
        const auto at = std::find(kRoles.begin(), kRoles.end(), r);
        return at == kRoles.end() ? kRoles.size() : static_cast<usize>(at - kRoles.begin());
    };
    const auto before =
        std::find_if(parts.begin(), parts.end(), [&](const Part& p) { return rank(p.role) > rank(role); });
    parts.insert(before, Part{std::string(role), std::string(path)});
    return {};
}

void FigureManifest::setCloth(std::span<const std::string> paths)
{
    std::erase_if(parts, [](const Part& p) { return p.role.starts_with(kClothPrefix); });
    for (const std::string& path : paths)
    {
        if (find(clothRole(path)) == nullptr)
        {
            parts.push_back({clothRole(path), path});
        }
    }
}

std::vector<std::string> FigureManifest::cloth() const
{
    std::vector<std::string> out;
    for (const Part& p : parts)
    {
        if (p.role.starts_with(kClothPrefix))
        {
            out.push_back(p.path);
        }
    }
    return out;
}

Result<SkinnedModelData> assembleFigure(const FigureManifest& manifest, std::span<const FigurePart> parts)
{
    const FigurePart* bodyPart = partOf(parts, "body");
    const FigurePart* headPart = partOf(parts, "head");
    for (const FigureManifest::Part& p : manifest.parts)
    {
        const FigurePart* loaded = partOf(parts, p.role);
        if (loaded == nullptr || loaded->data == nullptr)
        {
            return Error{std::format("part '{}' ({}) is not loaded", p.role, p.path)};
        }
    }
    if (bodyPart == nullptr || headPart == nullptr)
    {
        return Error{"a figure needs a body and a head"};
    }
    const SkinnedModelData& body = *bodyPart->data;
    const SkinnedModelData& head = *headPart->data;
    if (body.assembly.neck.empty() || head.assembly.neck.empty())
    {
        return Error{
            std::format("{} / {}: no neck data (gothar-chargen part-data)", bodyPart->path, headPart->path)};
    }
    // Garments: fitted to this body; pieces worn over the head drop whole roles (union over the pieces).
    const std::string& bodyPath = manifest.find("body")->path;
    std::vector<const FigurePart*> garments;
    std::vector<std::string> hidden;
    for (const FigureManifest::Part& p : manifest.parts)
    {
        if (!p.role.starts_with(kClothPrefix))
        {
            continue;
        }
        const FigurePart* g = partOf(parts, p.role);
        const SkinnedModelData::Assembly& data = g->data->assembly;
        if (data.coversBody.empty())
        {
            return Error{std::format("{}: no covers data", p.path)};
        }
        if (data.coversBody != bodyPath)
        {
            return Error{std::format("{} was fitted to {}, not to {}", p.path, data.coversBody, bodyPath)};
        }
        for (const std::string& role : data.hides)
        {
            if (std::find(kHideable.begin(), kHideable.end(), role) == kHideable.end())
            {
                return Error{std::format("{}: hides only hair and beard, not '{}'", p.path, role)};
            }
            if (std::find(hidden.begin(), hidden.end(), role) == hidden.end())
            {
                hidden.push_back(role);
            }
        }
        garments.push_back(g);
    }
    const auto isHidden = [&](std::string_view role)
    { return std::find(hidden.begin(), hidden.end(), role) != hidden.end(); };

    SkinnedModelData out;
    out.skeleton = body.skeleton;
    out.inverseBind = body.inverseBind;

    // Materials merged by name, the head first (its skin covers the body too); images by resolved path.
    std::vector<std::string> roles; // drawn roles: body, head, then the others in manifest order
    roles.emplace_back("body");
    roles.emplace_back("head");
    for (const FigureManifest::Part& p : manifest.parts)
    {
        if (p.role != "body" && p.role != "head" && !isHidden(p.role))
        {
            roles.push_back(p.role);
        }
    }
    std::vector<std::string> materialOrder{"head"};
    for (const FigureManifest::Part& p : manifest.parts)
    {
        if (p.role != "head" && !isHidden(p.role))
        {
            materialOrder.push_back(p.role);
        }
    }
    std::map<std::string, u32, std::less<>> materialOf;
    std::map<std::string, i32, std::less<>> imageOf;
    std::map<std::string, std::vector<u32>, std::less<>> partMaterial;
    for (const std::string& role : materialOrder)
    {
        const FigurePart& part = *partOf(parts, role);
        const SkinnedModelData& data = *part.data;
        const auto image = [&](i32 index) -> Result<i32>
        {
            if (index < 0)
            {
                return -1;
            }
            const ImageSource& source = data.images[static_cast<usize>(index)];
            if (source.uri.empty())
            {
                return Error{
                    std::format("{}: embedded images are not supported (externalize them)", part.path)};
            }
            const std::string path = resolveUri(part.path, source.uri);
            if (const auto found = imageOf.find(path); found != imageOf.end())
            {
                return found->second;
            }
            out.images.push_back(ImageSource{path, {}, source.mimeType});
            return imageOf[path] = static_cast<i32>(out.images.size() - 1);
        };
        auto& mapping = partMaterial[role];
        for (const MaterialInfo& material : data.materials)
        {
            const std::string base = baseName(material.name);
            if (!materialOf.contains(base))
            {
                MaterialInfo merged = material;
                merged.name = base;
                auto colour = image(material.baseColorImage);
                auto normal = image(material.normalImage);
                auto emissive = image(material.emissiveImage);
                if (!colour || !normal || !emissive)
                {
                    return !colour ? colour.error() : !normal ? normal.error() : emissive.error();
                }
                merged.baseColorImage = colour.value();
                merged.normalImage = normal.value();
                merged.emissiveImage = emissive.value();
                for (const auto& [name, rgb] : manifest.palette)
                {
                    if (name == base)
                    {
                        merged.baseColor = Vec4(rgb, 1.0f);
                    }
                }
                out.materials.push_back(merged);
                materialOf[base] = static_cast<u32>(out.materials.size() - 1);
            }
            mapping.push_back(materialOf[base]);
        }
    }

    // Parts per role and LOD level.
    std::map<u32, const SkinnedPartData*> headLods;
    for (const SkinnedPartData& p : head.parts)
    {
        headLods[p.lod] = &p;
    }
    for (const std::string& role : roles)
    {
        const FigurePart& part = *partOf(parts, role);
        const SkinnedModelData& data = *part.data;
        std::vector<u16> boneMap(data.skeleton.size());
        for (usize b = 0; b < data.skeleton.size(); ++b)
        {
            const i32 bone = body.skeleton.find(data.skeleton.names[b]);
            if (bone < 0)
            {
                return Error{std::format("{}: bone '{}' is not in the body's skeleton", part.path,
                                         data.skeleton.names[b])};
            }
            boneMap[b] = static_cast<u16>(bone);
        }
        std::vector<const SkinnedPartData*> levels;
        for (const SkinnedPartData& p : data.parts)
        {
            levels.push_back(&p);
        }
        std::stable_sort(levels.begin(), levels.end(), [](auto* a, auto* b) { return a->lod < b->lod; });
        for (const SkinnedPartData* source : levels)
        {
            SkinnedPartData result;
            result.node = std::format("{}_lod{}", role, source->lod);
            result.role = role;
            result.lod = source->lod;
            result.vertices = source->vertices;
            result.weights = source->weights;
            result.morphs = source->morphs;
            result.joints.resize(source->joints.size());
            for (usize v = 0; v < source->joints.size(); ++v)
            {
                for (usize j = 0; j < 4; ++j)
                {
                    const u16 bone = source->joints[v][j];
                    result.joints[v][j] = bone < boneMap.size() ? boneMap[bone] : bone;
                }
            }
            if (source->primitives.empty() && !source->indices.empty())
            {
                return Error{std::format("{}: {} has no primitive ranges (load it from glTF)", part.path,
                                         source->node)};
            }
            std::vector<bool> keep(source->indices.size() / 3, true);
            if (role == "body")
            {
                if (const auto headLod = headLods.find(source->lod); headLod != headLods.end())
                {
                    auto snapped = snapNeck(*source, body.assembly, *headLod->second, head.assembly);
                    if (!snapped)
                    {
                        return snapped.error();
                    }
                    for (usize v = 0; v < result.vertices.size(); ++v)
                    {
                        result.vertices[v].position = snapped.value()[v];
                    }
                }
                for (const FigurePart* garment : garments)
                {
                    const auto ranges = garment->data->assembly.covers.find(source->node);
                    if (ranges == garment->data->assembly.covers.end())
                    {
                        continue;
                    }
                    for (const auto& [primitive, first, end] : ranges->second)
                    {
                        if (primitive >= source->primitives.size() ||
                            end > source->primitives[primitive].indexCount / 3)
                        {
                            return Error{std::format("{}: covers [{}, {}, {}) outside {}", garment->path,
                                                     primitive, first, end, source->node)};
                        }
                        const u32 base = source->primitives[primitive].firstIndex / 3;
                        std::fill(keep.begin() + base + first, keep.begin() + base + end, false);
                    }
                }
            }
            // Triangles by new material, primitives in file order (as the loader groups the Python result).
            std::map<u32, std::vector<u32>> groups;
            for (const SkinnedPartData::Primitive& p : source->primitives)
            {
                if (p.indexCount == 0)
                {
                    continue;
                }
                const auto& mapping = partMaterial[role];
                const u32 material = p.material < mapping.size() ? mapping[p.material] : p.material;
                auto& group = groups[material];
                for (u32 t = p.firstIndex / 3; t < (p.firstIndex + p.indexCount) / 3; ++t)
                {
                    if (keep[t])
                    {
                        group.insert(group.end(), source->indices.begin() + t * 3,
                                     source->indices.begin() + t * 3 + 3);
                    }
                }
            }
            for (auto& [material, indices] : groups)
            {
                if (indices.empty())
                {
                    continue;
                }
                result.submeshes.push_back(
                    {static_cast<u32>(result.indices.size()), static_cast<u32>(indices.size()), material});
                result.indices.insert(result.indices.end(), indices.begin(), indices.end());
            }
            if (!result.indices.empty())
            {
                out.parts.push_back(std::move(result));
            }
        }
    }
    bool first = true;
    for (const SkinnedPartData& part : out.parts)
    {
        for (const Vertex& v : part.vertices)
        {
            out.bounds =
                first ? AABB{v.position, v.position}
                      : AABB{glm::min(out.bounds.min, v.position), glm::max(out.bounds.max, v.position)};
            first = false;
        }
    }
    return out;
}
} // namespace g7::asset
