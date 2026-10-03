// Skinned models and animation clips from glTF (M6 part A): the reference rig, the placeholder figure,
// the clip set none.glb with its events, events.toml errors, LOD selection.

#include <g7/asset/AssetManager.hpp>
#include <g7/asset/SkinnedModel.hpp>
#include <g7/asset/Vfs.hpp>
#include <g7/core/FileSystem.hpp>

#include <doctest/doctest.h>

#include <cmath>
#include <filesystem>
#include <ostream> // doctest needs it to print std::string operands
#include <string>
#include <tuple>
#include <vector>

using namespace g7;
using namespace g7::asset;

namespace
{
std::vector<u8> sourceFile(const char* relative)
{
    auto bytes = fs::readFile(fs::fromUtf8(std::string(G7_ASSET_SOURCE_DIR) + "/" + relative));
    REQUIRE_MESSAGE(bytes.ok(), relative);
    return std::move(bytes).value();
}

std::string sourceText(const char* relative)
{
    const std::vector<u8> bytes = sourceFile(relative);
    return std::string(bytes.begin(), bytes.end());
}

Mat4 localOf(const SkeletonData& s, usize i)
{
    return glm::translate(Mat4(1.0f), s.translations[i]) * glm::mat4_cast(s.rotations[i]) *
           glm::scale(Mat4(1.0f), s.scales[i]);
}

/// Model-space rest transforms of all bones.
std::vector<Mat4> restGlobals(const SkeletonData& s)
{
    std::vector<Mat4> global(s.size());
    for (usize i = 0; i < s.size(); ++i)
    {
        global[i] =
            (s.parents[i] < 0 ? s.rootParent : global[static_cast<usize>(s.parents[i])]) * localOf(s, i);
    }
    return global;
}

bool nearIdentity(const Mat4& m, f32 eps)
{
    for (int c = 0; c < 4; ++c)
    {
        for (int r = 0; r < 4; ++r)
        {
            if (std::abs(m[c][r] - (c == r ? 1.0f : 0.0f)) > eps)
            {
                return false;
            }
        }
    }
    return true;
}
} // namespace

TEST_CASE("Skinned glTF: the reference rig - skeleton order, inverse bind matrices, weights")
{
    const std::vector<u8> bytes = sourceFile("characters/rig/human_reference.glb");
    auto loaded = loadSkinnedGltf(bytes, {}, "human_reference.glb");
    REQUIRE_MESSAGE(loaded.ok(), (loaded.ok() ? "" : loaded.error().message));
    const SkinnedModelData& model = loaded.value();
    const SkeletonData& s = model.skeleton;
    CHECK(s.size() == 60); // animation.md
    for (const char* bone : {"root", "pelvis", "spine_03", "head", "hand_l", "hand_r", "foot_l",
                             "socket_hand_r", "socket_back_2h", "socket_helmet"})
    {
        CAPTURE(bone);
        CHECK(s.find(bone) >= 0);
    }
    CHECK(s.find("nope") == -1);
    CHECK(s.parents[static_cast<usize>(s.find("root"))] == -1);
    CHECK(s.parents[static_cast<usize>(s.find("head"))] == s.find("neck"));
    for (usize i = 0; i < s.size(); ++i)
    {
        CHECK(s.parents[i] < static_cast<i32>(i)); // parents first
    }
    // The inverse bind matrices undo the rest pose: global * inverse = identity for every bone.
    REQUIRE(model.inverseBind.size() == s.size());
    const std::vector<Mat4> global = restGlobals(s);
    for (usize i = 0; i < s.size(); ++i)
    {
        CAPTURE(s.names[i]);
        CHECK(nearIdentity(global[i] * model.inverseBind[i], 1e-3f));
    }
    // Head about 1.6 m up, feet on the ground (Y up, metres).
    CHECK(Vec3(global[static_cast<usize>(s.find("head"))][3]).y > 1.4f);
    CHECK(std::abs(Vec3(global[static_cast<usize>(s.find("root"))][3]).y) < 0.05f);

    REQUIRE_FALSE(model.parts.empty());
    usize morphs = 0;
    for (const SkinnedPartData& part : model.parts)
    {
        REQUIRE(part.joints.size() == part.vertices.size());
        REQUIRE(part.weights.size() == part.vertices.size());
        for (usize v = 0; v < part.vertices.size(); ++v)
        {
            const Vec4& w = part.weights[v];
            CHECK(w.x + w.y + w.z + w.w == doctest::Approx(1.0f).epsilon(1e-4));
            for (const u16 j : part.joints[v])
            {
                CHECK(j < s.size());
            }
        }
        for (const MorphTargetData& target : part.morphs)
        {
            CHECK(target.positions.size() == part.vertices.size());
        }
        morphs += part.morphs.size();
    }
    CHECK(morphs > 0); // the rig carries test morph targets
    CHECK(model.bounds.max.y > 1.6f);
}

TEST_CASE("Skinned glTF: the placeholder figure loads; files without a skin are refused")
{
    const std::vector<u8> bytes = sourceFile("characters/figures/placeholder_mannequin.glb");
    auto figure = loadSkinnedGltf(bytes, {}, "placeholder_mannequin.glb");
    REQUIRE_MESSAGE(figure.ok(), (figure.ok() ? "" : figure.error().message));
    CHECK(figure.value().skeleton.size() == 60);

    const std::vector<u8> block = sourceFile("testworld/block.gltf");
    auto noSkin =
        loadSkinnedGltf(block, fs::fromUtf8(std::string(G7_ASSET_SOURCE_DIR) + "/testworld"), "block.gltf");
    REQUIRE_FALSE(noSkin.ok());
    CHECK(noSkin.error().message.find("no skin") != std::string::npos);
}

TEST_CASE("Animation set: the clips of none.glb with the events of none.events.toml")
{
    const std::vector<u8> bytes = sourceFile("characters/anims/human/none.glb");
    auto loaded = loadAnimationGltf(bytes, {}, "none.glb");
    REQUIRE_MESSAGE(loaded.ok(), (loaded.ok() ? "" : loaded.error().message));
    AnimationSetData& set = loaded.value();
    CHECK(set.skeleton.size() == 60);
    CHECK(set.clips.size() >= 20);
    const ClipData* walk = set.find("none/s_walk");
    REQUIRE(walk != nullptr);
    CHECK(walk->loops());
    CHECK(walk->duration > 0.3f);
    CHECK_FALSE(set.find("none/t_climb_low")->loops());
    CHECK(set.find("none/missing") == nullptr);
    // Every track names a bone of the reference skeleton; root moves for the climbing clips.
    for (const ClipData& clip : set.clips)
    {
        for (const TrackData& track : clip.tracks)
        {
            CAPTURE(clip.name);
            CAPTURE(track.bone);
            CHECK(set.skeleton.find(track.bone) >= 0);
            CHECK(track.times.back() <= clip.duration + 1e-5f);
        }
    }
    const ClipData* climb = set.find("none/t_climb_mid");
    REQUIRE(climb != nullptr);
    f32 rootRise = 0.0f;
    for (const TrackData& track : climb->tracks)
    {
        if (track.bone == "root" && track.path == TrackData::Path::Translation)
        {
            rootRise = track.values.back().y - track.values.front().y;
        }
    }
    CHECK(rootRise == doctest::Approx(1.6f).epsilon(0.05)); // built for 1.6 m (figuren)

    REQUIRE(
        applyClipEvents(set, sourceText("characters/anims/human/none.events.toml"), "none.events.toml").ok());
    const ClipData* run = set.find("none/s_run");
    REQUIRE(run != nullptr);
    REQUIRE(run->events.size() >= 2);
    CHECK(run->events[0].name == "footstep_l");
    CHECK(run->events[0].time == doctest::Approx(1.0f / 30.0f));
    for (usize i = 1; i < run->events.size(); ++i)
    {
        CHECK(run->events[i].time >= run->events[i - 1].time);
    }
}

TEST_CASE("Animation set: events.toml errors")
{
    AnimationSetData set;
    set.clips.push_back({"none/s_walk", 1.0f, {}, {}, 0.0f});
    set.clips.push_back({"none/t_jump_land", 0.5f, {}, {}, 0.0f});
    const auto fails = [&](const char* toml, const char* expected)
    {
        auto copy = set;
        auto result = applyClipEvents(copy, toml, "x.events.toml");
        REQUIRE_FALSE(result.ok());
        CHECK_MESSAGE(result.error().message.find(expected) != std::string::npos, result.error().message);
    };
    fails("fps = 30\n", "version = 1");
    fails("version = 1\n[clips.\"none/s_jog\"]\nevents = [{ frame = 0, event = \"a\" }]\n", "unknown clip");
    fails("version = 1\n[clips.\"none/s_walk\"]\nevents = [{ frame = 30, event = \"a\" }]\n",
          "outside"); // loop end
    fails("version = 1\n[clips.\"none/t_jump_land\"]\nevents = [{ frame = 16, event = \"a\" }]\n", "outside");
    fails("version = 1\n[clips.\"none/s_walk\"]\nevents = [{ frame = 5, event = \"a\" }, { frame = 2, event "
          "= \"b\" }]\n",
          "rise");
    fails("version = 1\n[clips.\"none/s_walk\"]\nevents = [{ frame = 5 }]\n", "needs 'frame'");
    fails("version = 1\n[clips.\"none/s_walk\"]\nspeed = 0.0\n", "'speed' must be positive");
    fails("version = 1\n[clips.\"none/s_walk\"]\nspeed = -1.0\n", "'speed' must be positive");

    auto copy = set;
    REQUIRE(
        applyClipEvents(copy,
                        "version = 1\nfps = 10\n[clips.\"none/t_jump_land\"]\n"
                        "events = [{ frame = 5, event = \"land\" }, { frame = 5, event = \"sound:thud\" }]\n",
                        "ok.events.toml")
            .ok());
    REQUIRE(copy.clips[1].events.size() == 2);
    CHECK(copy.clips[1].events[0].time == doctest::Approx(0.5f)); // last frame of a one-shot is allowed
    CHECK(copy.clips[1].events[1].name == "sound:thud");          // same frame: file order
    CHECK(copy.clips[0].speed == 0.0f);                           // no speed: unknown

    // Own speed of a clip (m/s), with or without events (M6, additive in version 1).
    copy = set;
    REQUIRE(
        applyClipEvents(copy, "version = 1\n[clips.\"none/s_walk\"]\nspeed = 1.05\n", "s.events.toml").ok());
    CHECK(copy.clips[0].speed == doctest::Approx(1.05f));
    CHECK(copy.clips[0].events.empty());
}

TEST_CASE("Skinned model: parts per LOD level fall back to the nearest coarser one")
{
    SkinnedModelData model;
    for (const auto& [node, role, lod] :
         {std::tuple{"body_lod0", "body", 0u}, std::tuple{"body_lod1", "body", 1u},
          std::tuple{"body_lod2", "body", 2u}, std::tuple{"hair_lod0", "hair", 0u},
          std::tuple{"belt", "belt", 0u}})
    {
        SkinnedPartData part;
        part.node = node;
        part.role = role;
        part.lod = lod;
        model.parts.push_back(part);
    }
    CHECK(model.lodCount() == 3);
    const auto names = [&](u32 lod)
    {
        std::string out;
        for (const SkinnedPartData* p : model.partsForLod(lod))
        {
            out += p->node + " ";
        }
        return out;
    };
    CHECK(names(0) == "body_lod0 hair_lod0 belt ");
    CHECK(names(1) == "body_lod1 hair_lod0 belt ");
    CHECK(names(5) == "body_lod2 hair_lod0 belt ");
}

TEST_CASE("AssetManager: figures and animation sets load, the set with its events.toml")
{
    Vfs vfs;
    REQUIRE(vfs.mount(fs::fromUtf8(G7_ASSET_SOURCE_DIR), 0));
    AssetManager manager(vfs);
    auto figure = manager.load<SkinnedModelData>("characters/figures/placeholder_mannequin.glb");
    auto set = manager.load<AnimationSetData>("characters/anims/human/none.glb");
    auto swim = manager.load<AnimationSetData>("characters/anims/human/swim.glb"); // has no events.toml
    manager.waitAll();
    manager.update();
    REQUIRE_MESSAGE(figure.isReady(), figure.error());
    REQUIRE_MESSAGE(set.isReady(), set.error());
    REQUIRE_MESSAGE(swim.isReady(), swim.error());
    CHECK(figure.get()->skeleton.size() == 60);
    const ClipData* walk = set.get()->find("none/s_walk");
    REQUIRE(walk != nullptr);
    CHECK_FALSE(walk->events.empty()); // from none.events.toml
    CHECK(swim.get()->find("swim/s_forward") != nullptr);
}

// Adapted from the check by figuren (M6 A review): every human clip set and every assembled figure shares the
// reference skeleton - same bones, same order, joints within 1 mm - and figures bind consistently. Figures
// exist only after `g7_figures` (not versioned); without them only the sets are checked.
TEST_CASE("Skeletons of the human clip sets and figures equal the reference rig")
{
    const std::vector<u8> rigBytes = sourceFile("characters/rig/human_reference.glb");
    auto rigLoaded = loadSkinnedGltf(rigBytes, {}, "rig");
    REQUIRE(rigLoaded.ok());
    const SkeletonData& rig = rigLoaded.value().skeleton;
    const std::vector<Mat4> rigGlobal = restGlobals(rig);
    const auto compare = [&](const SkeletonData& s, const std::string& what)
    {
        CAPTURE(what);
        REQUIRE(s.size() == rig.size());
        const std::vector<Mat4> g = restGlobals(s);
        for (usize i = 0; i < s.size(); ++i)
        {
            CHECK(s.names[i] == rig.names[i]);
            CHECK(glm::length(Vec3(g[i][3]) - Vec3(rigGlobal[i][3])) < 0.001f);
        }
    };
    const std::filesystem::path root = std::filesystem::path(G7_ASSET_SOURCE_DIR) / "characters";
    usize sets = 0;
    for (const auto& entry : std::filesystem::directory_iterator(root / "anims" / "human"))
    {
        if (entry.path().extension() == ".glb")
        {
            const std::string name = entry.path().filename().generic_string();
            auto set = loadAnimationGltf(fs::readFile(entry.path()).value(), {}, name);
            REQUIRE_MESSAGE(set.ok(), name);
            compare(set.value().skeleton, name);
            ++sets;
        }
    }
    CHECK(sets >= 9);
    usize figures = 0;
    for (const auto& entry : std::filesystem::directory_iterator(root / "figures"))
    {
        const std::string name = entry.path().filename().generic_string();
        if (entry.path().extension() != ".glb" || name == "placeholder_mannequin.glb")
        {
            continue;
        }
        auto figure = loadSkinnedGltf(fs::readFile(entry.path()).value(), {}, name);
        REQUIRE_MESSAGE(figure.ok(), name);
        compare(figure.value().skeleton, name);
        const std::vector<Mat4> g = restGlobals(figure.value().skeleton);
        for (usize i = 0; i < g.size(); ++i)
        {
            CHECK(nearIdentity(g[i] * figure.value().inverseBind[i], 1e-3f));
        }
        ++figures;
    }
    if (figures == 0)
    {
        MESSAGE("no assembled figures (build g7_figures): only the clip sets were checked");
    }
}
