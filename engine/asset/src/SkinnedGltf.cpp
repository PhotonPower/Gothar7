#include "GltfCommon.hpp"

#include <g7/asset/SkinnedModel.hpp>
#include <g7/core/Config.hpp>
#include <g7/core/Log.hpp>

#include <fastgltf/glm_element_traits.hpp>
#include <fastgltf/tools.hpp>

#include <algorithm>
#include <charconv>
#include <format>
#include <map>
#include <numeric>

namespace g7::asset
{
i32 SkeletonData::find(std::string_view name) const noexcept
{
    for (usize i = 0; i < names.size(); ++i)
    {
        if (names[i] == name)
        {
            return static_cast<i32>(i);
        }
    }
    return -1;
}

std::vector<const SkinnedPartData*> SkinnedModelData::partsForLod(u32 lod) const
{
    // Per role the part with the requested level, else the nearest coarser-or-finer one below it.
    std::map<std::string, const SkinnedPartData*> best;
    for (const SkinnedPartData& part : parts)
    {
        auto [it, inserted] = best.try_emplace(part.role, &part);
        if (inserted)
        {
            continue;
        }
        const u32 current = it->second->lod;
        const bool better =
            part.lod <= lod ? (current > lod || part.lod > current) : (current > lod && part.lod < current);
        if (better)
        {
            it->second = &part;
        }
    }
    std::vector<const SkinnedPartData*> result;
    for (const SkinnedPartData& part : parts) // file order
    {
        if (best[part.role] == &part)
        {
            result.push_back(&part);
        }
    }
    return result;
}

u32 SkinnedModelData::lodCount() const noexcept
{
    u32 count = 0;
    for (const SkinnedPartData& part : parts)
    {
        count = std::max(count, part.lod + 1);
    }
    return count;
}

bool ClipData::loops() const noexcept
{
    const usize slash = name.rfind('/');
    const std::string_view action =
        slash == std::string::npos ? std::string_view(name) : std::string_view(name).substr(slash + 1);
    return action.starts_with("s_");
}

const ClipData* AnimationSetData::find(std::string_view name) const noexcept
{
    for (const ClipData& clip : clips)
    {
        if (clip.name == name)
        {
            return &clip;
        }
    }
    return nullptr;
}

namespace
{
using namespace gltf;

Error fail(std::string_view debugName, const std::string& what)
{
    return Error{std::string(debugName) + ": " + what};
}

void localTrs(const fastgltf::Node& node, Vec3& t, Quat& r, Vec3& s)
{
    if (const auto* trs = std::get_if<fastgltf::TRS>(&node.transform))
    {
        t = Vec3(trs->translation[0], trs->translation[1], trs->translation[2]);
        r = Quat(trs->rotation[3], trs->rotation[0], trs->rotation[1], trs->rotation[2]);
        s = Vec3(trs->scale[0], trs->scale[1], trs->scale[2]);
        return;
    }
    const Mat4 m = toGlm(std::get<fastgltf::math::fmat4x4>(node.transform));
    t = Vec3(m[3]);
    s = Vec3(glm::length(Vec3(m[0])), glm::length(Vec3(m[1])), glm::length(Vec3(m[2])));
    r = glm::normalize(glm::quat_cast(Mat3(Vec3(m[0]) / s.x, Vec3(m[1]) / s.y, Vec3(m[2]) / s.z)));
}

Mat4 localMatrix(const fastgltf::Node& node)
{
    Vec3 t;
    Quat r;
    Vec3 s;
    localTrs(node, t, r, s);
    return glm::translate(Mat4(1.0f), t) * glm::mat4_cast(r) * glm::scale(Mat4(1.0f), s);
}

/// Parent node of every node (-1: none).
std::vector<i64> nodeParents(const fastgltf::Asset& asset)
{
    std::vector<i64> parents(asset.nodes.size(), -1);
    for (usize n = 0; n < asset.nodes.size(); ++n)
    {
        for (const usize child : asset.nodes[n].children)
        {
            parents[child] = static_cast<i64>(n);
        }
    }
    return parents;
}

/// The skeleton from a set of joint nodes: sorted parents first; `jointToBone` maps the given order.
SkeletonData buildSkeleton(const fastgltf::Asset& asset, const std::vector<usize>& joints,
                           std::vector<u32>& jointToBone)
{
    const std::vector<i64> parents = nodeParents(asset);
    const auto depth = [&](usize node)
    {
        u32 d = 0;
        for (i64 p = parents[node]; p >= 0; p = parents[static_cast<usize>(p)])
        {
            ++d;
        }
        return d;
    };
    std::vector<usize> order(joints.size());
    std::iota(order.begin(), order.end(), usize{0});
    std::stable_sort(order.begin(), order.end(),
                     [&](usize a, usize b) { return depth(joints[a]) < depth(joints[b]); });

    SkeletonData skeleton;
    jointToBone.assign(joints.size(), 0);
    std::map<usize, i32> boneOfNode;
    for (const usize j : order)
    {
        const usize node = joints[j];
        const i32 bone = static_cast<i32>(skeleton.names.size());
        jointToBone[j] = static_cast<u32>(bone);
        boneOfNode[node] = bone;
        // Nearest ancestor that is a joint.
        i32 parentBone = -1;
        for (i64 p = parents[node]; p >= 0 && parentBone < 0; p = parents[static_cast<usize>(p)])
        {
            const auto found = boneOfNode.find(static_cast<usize>(p));
            if (found != boneOfNode.end())
            {
                parentBone = found->second;
            }
        }
        if (parentBone < 0 && skeleton.names.empty())
        {
            // The nodes above the first root bone (the armature) - accumulated.
            Mat4 above(1.0f);
            for (i64 p = parents[node]; p >= 0; p = parents[static_cast<usize>(p)])
            {
                above = localMatrix(asset.nodes[static_cast<usize>(p)]) * above;
            }
            skeleton.rootParent = above;
        }
        Vec3 t;
        Quat r;
        Vec3 s;
        localTrs(asset.nodes[node], t, r, s);
        skeleton.names.emplace_back(asset.nodes[node].name);
        skeleton.parents.push_back(parentBone);
        skeleton.translations.push_back(t);
        skeleton.rotations.push_back(r);
        skeleton.scales.push_back(s);
    }
    return skeleton;
}

/// "body_lod1" -> role "body", lod 1; without suffix lod 0.
void splitLod(const std::string& node, std::string& role, u32& lod)
{
    role = node;
    lod = 0;
    const usize at = node.rfind("_lod");
    if (at == std::string::npos || at + 4 >= node.size())
    {
        return;
    }
    u32 value = 0;
    const char* first = node.data() + at + 4;
    const char* last = node.data() + node.size();
    if (const auto [end, ec] = std::from_chars(first, last, value); ec == std::errc{} && end == last)
    {
        role = node.substr(0, at);
        lod = value;
    }
}

Result<void> appendSkinnedPrimitive(const fastgltf::Asset& asset, const fastgltf::Primitive& primitive,
                                    u32 material, const std::vector<u32>& jointToBone, SkinnedPartData& part,
                                    std::map<u32, std::vector<u32>>& byMaterial, bool needsTangents)
{
    const auto attribute = [&](const char* name) -> const fastgltf::Accessor*
    {
        const auto it = primitive.findAttribute(name);
        return it == primitive.attributes.end() ? nullptr : &asset.accessors[it->accessorIndex];
    };
    const fastgltf::Accessor* positions = attribute("POSITION");
    const fastgltf::Accessor* joints = attribute("JOINTS_0");
    const fastgltf::Accessor* weights = attribute("WEIGHTS_0");
    if (positions == nullptr || joints == nullptr || weights == nullptr)
    {
        return Error{"skinned primitive needs POSITION, JOINTS_0 and WEIGHTS_0"};
    }
    const usize first = part.vertices.size();
    const usize count = positions->count;
    part.vertices.resize(first + count);
    part.joints.resize(first + count);
    part.weights.resize(first + count);
    fastgltf::iterateAccessorWithIndex<Vec3>(asset, *positions,
                                             [&](Vec3 p, usize i) { part.vertices[first + i].position = p; });
    const fastgltf::Accessor* normals = attribute("NORMAL");
    if (normals != nullptr)
    {
        fastgltf::iterateAccessorWithIndex<Vec3>(asset, *normals, [&](Vec3 n, usize i)
                                                 { part.vertices[first + i].normal = glm::normalize(n); });
    }
    if (const fastgltf::Accessor* uv = attribute("TEXCOORD_0"))
    {
        fastgltf::iterateAccessorWithIndex<Vec2>(asset, *uv,
                                                 [&](Vec2 t, usize i) { part.vertices[first + i].uv = t; });
    }
    const fastgltf::Accessor* tangents = attribute("TANGENT");
    if (tangents != nullptr)
    {
        fastgltf::iterateAccessorWithIndex<Vec4>(asset, *tangents, [&](Vec4 t, usize i)
                                                 { part.vertices[first + i].tangent = t; });
    }
    bool badJoint = false;
    fastgltf::iterateAccessorWithIndex<glm::u16vec4>(asset, *joints,
                                                     [&](glm::u16vec4 j, usize i)
                                                     {
                                                         for (int k = 0; k < 4; ++k)
                                                         {
                                                             if (j[k] >= jointToBone.size())
                                                             {
                                                                 badJoint = true;
                                                                 continue;
                                                             }
                                                             part.joints[first + i][static_cast<usize>(k)] =
                                                                 static_cast<u16>(jointToBone[j[k]]);
                                                         }
                                                     });
    if (badJoint)
    {
        return Error{"joint index outside the skin"};
    }
    fastgltf::iterateAccessorWithIndex<Vec4>(asset, *weights,
                                             [&](Vec4 w, usize i)
                                             {
                                                 const f32 sum = w.x + w.y + w.z + w.w;
                                                 part.weights[first + i] =
                                                     sum > 1e-6f ? w / sum : Vec4(1, 0, 0, 0);
                                             });

    std::vector<u32> indices;
    if (primitive.indicesAccessor)
    {
        const fastgltf::Accessor& accessor = asset.accessors[*primitive.indicesAccessor];
        indices.resize(accessor.count);
        fastgltf::copyFromAccessor<u32>(asset, accessor, indices.data());
    }
    else
    {
        indices.resize(count);
        std::iota(indices.begin(), indices.end(), 0u);
    }
    for (u32& index : indices)
    {
        if (index >= count)
        {
            return Error{"index out of range"};
        }
        index += static_cast<u32>(first);
    }
    if (normals == nullptr)
    {
        computeFlatNormals(part.vertices, indices, first);
    }
    if (needsTangents && tangents == nullptr)
    {
        computeTangents(part.vertices, indices, first);
    }

    // Morph targets: offsets per vertex, appended to the part's targets in order.
    if (part.morphs.size() < primitive.targets.size())
    {
        part.morphs.resize(primitive.targets.size());
    }
    for (usize t = 0; t < part.morphs.size(); ++t)
    {
        MorphTargetData& target = part.morphs[t];
        target.positions.resize(first + count, Vec3(0.0f));
        if (t >= primitive.targets.size())
        {
            continue; // this primitive has fewer targets: no offsets
        }
        for (const fastgltf::Attribute& a : primitive.targets[t])
        {
            const fastgltf::Accessor& accessor = asset.accessors[a.accessorIndex];
            if (a.name == "POSITION")
            {
                fastgltf::iterateAccessorWithIndex<Vec3>(asset, accessor, [&](Vec3 d, usize i)
                                                         { target.positions[first + i] = d; });
            }
            else if (a.name == "NORMAL")
            {
                target.normals.resize(first + count, Vec3(0.0f));
                fastgltf::iterateAccessorWithIndex<Vec3>(asset, accessor, [&](Vec3 d, usize i)
                                                         { target.normals[first + i] = d; });
            }
        }
    }
    auto& target = byMaterial[material];
    target.insert(target.end(), indices.begin(), indices.end());
    return {};
}

Result<SkinnedModelData> convertSkinned(const fastgltf::Asset& asset, std::string_view debugName)
{
    if (asset.skins.empty())
    {
        return fail(debugName, "no skin in this glTF (not a skinned model)");
    }
    const fastgltf::Skin& skin = asset.skins.front();
    if (skin.joints.size() > kMaxBones)
    {
        return fail(debugName, std::format("{} bones, at most {} allowed", skin.joints.size(), kMaxBones));
    }
    SkinnedModelData model;
    std::vector<u32> jointToBone;
    model.skeleton =
        buildSkeleton(asset, std::vector<usize>(skin.joints.begin(), skin.joints.end()), jointToBone);
    model.inverseBind.assign(skin.joints.size(), Mat4(1.0f));
    if (skin.inverseBindMatrices)
    {
        fastgltf::iterateAccessorWithIndex<fastgltf::math::fmat4x4>(
            asset, asset.accessors[*skin.inverseBindMatrices],
            [&](const fastgltf::math::fmat4x4& m, usize i)
            {
                if (i < jointToBone.size())
                {
                    model.inverseBind[jointToBone[i]] = toGlm(m);
                }
            });
    }
    model.materials = readMaterials(asset);
    model.images = readImages(asset, debugName);
    const u32 defaultMaterial = static_cast<u32>(model.materials.size());
    bool usesDefault = false;

    for (usize n = 0; n < asset.nodes.size(); ++n)
    {
        const fastgltf::Node& node = asset.nodes[n];
        if (!node.meshIndex || !node.skinIndex)
        {
            continue; // unskinned meshes (helpers, sockets) are not part of the figure
        }
        SkinnedPartData part;
        part.node = std::string(node.name);
        splitLod(part.node, part.role, part.lod);
        std::map<u32, std::vector<u32>> byMaterial;
        for (const fastgltf::Primitive& primitive : asset.meshes[*node.meshIndex].primitives)
        {
            if (primitive.type != fastgltf::PrimitiveType::Triangles)
            {
                continue;
            }
            const u32 material =
                primitive.materialIndex ? static_cast<u32>(*primitive.materialIndex) : defaultMaterial;
            usesDefault = usesDefault || material == defaultMaterial;
            const bool needsTangents =
                material < model.materials.size() && model.materials[material].normalImage >= 0;
            if (auto added = appendSkinnedPrimitive(asset, primitive, material, jointToBone, part, byMaterial,
                                                    needsTangents);
                !added)
            {
                return fail(debugName, part.node + ": " + added.error().message);
            }
        }
        for (auto& [material, indices] : byMaterial)
        {
            part.submeshes.push_back(
                {static_cast<u32>(part.indices.size()), static_cast<u32>(indices.size()), material});
            part.indices.insert(part.indices.end(), indices.begin(), indices.end());
        }
        if (!part.indices.empty())
        {
            model.parts.push_back(std::move(part));
        }
    }
    if (usesDefault)
    {
        model.materials.push_back(MaterialInfo{.name = "default"});
    }
    if (model.parts.empty())
    {
        return fail(debugName, "the skin has no skinned meshes");
    }
    bool first = true;
    for (const SkinnedPartData& part : model.parts)
    {
        for (const Vertex& v : part.vertices)
        {
            model.bounds =
                first ? AABB{v.position, v.position}
                      : AABB{glm::min(model.bounds.min, v.position), glm::max(model.bounds.max, v.position)};
            first = false;
        }
    }
    return model;
}

Result<AnimationSetData> convertAnimations(const fastgltf::Asset& asset, std::string_view debugName)
{
    AnimationSetData set;
    std::vector<usize> joints;
    if (!asset.skins.empty())
    {
        joints.assign(asset.skins.front().joints.begin(), asset.skins.front().joints.end());
    }
    else
    {
        joints.resize(asset.nodes.size());
        std::iota(joints.begin(), joints.end(), usize{0});
    }
    std::vector<u32> jointToBone;
    set.skeleton = buildSkeleton(asset, joints, jointToBone);

    for (const fastgltf::Animation& animation : asset.animations)
    {
        ClipData clip;
        clip.name = std::string(animation.name);
        for (const fastgltf::AnimationChannel& channel : animation.channels)
        {
            if (!channel.nodeIndex || channel.path == fastgltf::AnimationPath::Weights)
            {
                continue; // morph weight animation: not used by the figures
            }
            const fastgltf::AnimationSampler& sampler = animation.samplers[channel.samplerIndex];
            if (sampler.interpolation == fastgltf::AnimationInterpolation::CubicSpline)
            {
                return fail(debugName, clip.name + ": cubic spline keys are not supported (export linear)");
            }
            TrackData track;
            track.bone = std::string(asset.nodes[*channel.nodeIndex].name);
            track.path = channel.path == fastgltf::AnimationPath::Translation ? TrackData::Path::Translation
                         : channel.path == fastgltf::AnimationPath::Rotation  ? TrackData::Path::Rotation
                                                                              : TrackData::Path::Scale;
            track.step = sampler.interpolation == fastgltf::AnimationInterpolation::Step;
            const fastgltf::Accessor& input = asset.accessors[sampler.inputAccessor];
            const fastgltf::Accessor& output = asset.accessors[sampler.outputAccessor];
            track.times.resize(input.count);
            fastgltf::copyFromAccessor<f32>(asset, input, track.times.data());
            track.values.resize(output.count);
            if (track.path == TrackData::Path::Rotation)
            {
                fastgltf::iterateAccessorWithIndex<Vec4>(asset, output,
                                                         [&](Vec4 q, usize i) { track.values[i] = q; });
            }
            else
            {
                fastgltf::iterateAccessorWithIndex<Vec3>(asset, output, [&](Vec3 v, usize i)
                                                         { track.values[i] = Vec4(v, 0.0f); });
            }
            if (track.times.empty() || track.values.size() != track.times.size() ||
                !std::is_sorted(track.times.begin(), track.times.end()))
            {
                return fail(debugName, clip.name + ": broken keys for " + track.bone);
            }
            clip.duration = std::max(clip.duration, track.times.back());
            clip.tracks.push_back(std::move(track));
        }
        set.clips.push_back(std::move(clip));
    }
    return set;
}

template <typename T, typename Convert>
Result<T> load(std::span<const u8> bytes, const fs::Path& baseDirectory, std::string_view debugName,
               Convert convert)
{
    auto data =
        fastgltf::GltfDataBuffer::FromBytes(reinterpret_cast<const std::byte*>(bytes.data()), bytes.size());
    if (data.error() != fastgltf::Error::None)
    {
        return fail(debugName, std::string(fastgltf::getErrorMessage(data.error())));
    }
    auto asset = parseAsset(data.get(), baseDirectory, debugName);
    if (!asset)
    {
        return asset.error();
    }
    return convert(asset.value(), debugName);
}
} // namespace

Result<SkinnedModelData> loadSkinnedGltf(std::span<const u8> bytes, const fs::Path& baseDirectory,
                                         std::string_view debugName)
{
    return load<SkinnedModelData>(bytes, baseDirectory, debugName, convertSkinned);
}

Result<AnimationSetData> loadAnimationGltf(std::span<const u8> bytes, const fs::Path& baseDirectory,
                                           std::string_view debugName)
{
    return load<AnimationSetData>(bytes, baseDirectory, debugName, convertAnimations);
}

Result<void> applyClipEvents(AnimationSetData& set, std::string_view toml, std::string_view source)
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
    const f64 fps = c.get<f64>("fps", 30.0);
    if (!(fps > 0.0))
    {
        return fail(source, "'fps' must be positive");
    }
    for (const std::string& name : c.keys("clips"))
    {
        ClipData* clip = nullptr;
        for (ClipData& candidate : set.clips)
        {
            clip = candidate.name == name ? &candidate : clip;
        }
        if (clip == nullptr)
        {
            return fail(source, std::format("events for unknown clip '{}'", name));
        }
        const std::string array = "clips." + name + ".events";
        const f64 lastFrame = static_cast<f64>(clip->duration) * fps;
        i64 previous = -1;
        std::vector<ClipEvent> events;
        for (usize i = 0; i < c.arraySize(array); ++i)
        {
            const std::string at = std::format("{}[{}]", array, i);
            const auto frame = c.find<i64>(at + ".frame");
            const auto event = c.find<std::string>(at + ".event");
            if (!frame || !event || event->empty())
            {
                return fail(source, std::format("{} needs 'frame' (integer) and 'event' (name)", at));
            }
            // Loops: the last frame equals frame 0, so it carries no event of its own.
            const bool outside = *frame < 0 || static_cast<f64>(*frame) > lastFrame + 1e-3 ||
                                 (clip->loops() && static_cast<f64>(*frame) >= lastFrame - 1e-3);
            if (outside)
            {
                return fail(source, std::format("{}: frame {} lies outside the clip (last frame {:.0f})", at,
                                                *frame, lastFrame));
            }
            if (*frame < previous)
            {
                return fail(source, std::format("{}: frames must rise", at));
            }
            previous = *frame;
            events.push_back({static_cast<f32>(static_cast<f64>(*frame) / fps), *event});
        }
        clip->events = std::move(events);
    }
    return {};
}
} // namespace g7::asset
