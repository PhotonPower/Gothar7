// Animation runtime (M6 part B, ADR 0019): skeleton, sampling, blending, events, root motion, the state
// machine - with a small hand-made skeleton, then with the real rig, clip sets and human.animgraph.toml.

#include <g7/animation/Animator.hpp>
#include <g7/animation/Clip.hpp>
#include <g7/animation/Skeleton.hpp>
#include <g7/asset/SkinnedModel.hpp>
#include <g7/core/FileSystem.hpp>

#include <doctest/doctest.h>

#include <cmath>
#include <ostream> // doctest needs it to print std::string operands
#include <string>
#include <vector>

using namespace g7;
using namespace g7::animation;

namespace
{
/// root (origin) - spine (1 m up) - arm (1 m along x).
asset::SkeletonData threeBones()
{
    asset::SkeletonData s;
    s.names = {"root", "spine", "arm"};
    s.parents = {-1, 0, 1};
    s.translations = {Vec3(0.0f), Vec3(0, 1, 0), Vec3(1, 0, 0)};
    s.rotations = {Quat(1, 0, 0, 0), Quat(1, 0, 0, 0), Quat(1, 0, 0, 0)};
    s.scales = {Vec3(1.0f), Vec3(1.0f), Vec3(1.0f)};
    return s;
}

Vec4 quatKey(f32 degreesAboutZ)
{
    const Quat q = glm::angleAxis(glm::radians(degreesAboutZ), Vec3(0, 0, 1));
    return Vec4(q.x, q.y, q.z, q.w);
}

asset::ClipData armSwing(std::string name, bool step = false)
{
    asset::ClipData c;
    c.name = std::move(name);
    c.duration = 1.0f;
    c.tracks.push_back(
        {"arm", asset::TrackData::Path::Rotation, step, {0.0f, 1.0f}, {quatKey(0), quatKey(90)}});
    return c;
}

f32 armAngle(const Pose& pose)
{
    return glm::degrees(glm::angle(pose[2].rotation)) * (pose[2].rotation.z < 0.0f ? -1.0f : 1.0f);
}

bool near(const Vec3& a, const Vec3& b, f32 eps = 1e-3f)
{
    return glm::length(a - b) < eps;
}
} // namespace

TEST_CASE("Skeleton: model space, masks, order")
{
    auto skeleton = Skeleton::create(threeBones());
    REQUIRE(skeleton.ok());
    const Skeleton& s = skeleton.value();
    CHECK(s.size() == 3);
    CHECK(s.find("arm") == 2);
    std::vector<Mat4> m(3);
    Pose pose = s.restPose();
    pose[1].rotation = glm::angleAxis(glm::radians(90.0f), Vec3(0, 0, 1)); // spine turns: the arm points up
    s.modelSpace(pose, m);
    CHECK(near(Vec3(m[1][3]), Vec3(0, 1, 0)));
    CHECK(near(Vec3(m[2][3]), Vec3(0, 2, 0)));
    CHECK(s.maskBelow("spine") == std::vector<f32>{0.0f, 1.0f, 1.0f});
    CHECK(s.maskBelow("nope") == std::vector<f32>{0.0f, 0.0f, 0.0f});

    asset::SkeletonData bad = threeBones();
    bad.parents = {-1, 2, 0};
    CHECK_FALSE(Skeleton::create(bad).ok());
    CHECK_FALSE(Skeleton::create({}).ok());
}

TEST_CASE("Clip: linear, slerp and step keys; loops wrap, one-shots clamp; unknown bones dropped")
{
    const Skeleton s = Skeleton::create(threeBones()).value();
    asset::ClipData data = armSwing("none/s_swing");
    data.tracks.push_back({"tail", asset::TrackData::Path::Rotation, false, {0.0f}, {quatKey(0)}});
    const Clip loop(data, s);
    CHECK(loop.loops());
    CHECK(loop.droppedTracks() == 1);
    Pose pose = s.restPose();
    loop.sample(0.5f, pose);
    CHECK(armAngle(pose) == doctest::Approx(45.0f).epsilon(0.001));
    loop.sample(1.25f, pose); // wraps to 0.25
    CHECK(armAngle(pose) == doctest::Approx(22.5f).epsilon(0.001));

    const Clip once(armSwing("none/t_swing"), s);
    CHECK_FALSE(once.loops());
    once.sample(5.0f, pose);
    CHECK(armAngle(pose) == doctest::Approx(90.0f).epsilon(0.001));
    const Clip stepped(armSwing("none/t_step", true), s);
    stepped.sample(0.9f, pose);
    CHECK(armAngle(pose) == doctest::Approx(0.0f).epsilon(0.001));
}

TEST_CASE("Clip: events in (from, to], across loop ends and several cycles")
{
    const Skeleton s = Skeleton::create(threeBones()).value();
    asset::ClipData data = armSwing("none/s_walk");
    data.events = {{0.25f, "footstep_l"}, {0.75f, "footstep_r"}, {0.75f, "sound:step"}};
    const Clip walk(data, s);
    std::string fired;
    const EventCallback log = [&](std::string_view, std::string_view e) { fired += std::string(e) + " "; };
    walk.fireEvents(0.5f, 1.6f, log);
    CHECK(fired == "footstep_r sound:step footstep_l ");
    fired.clear();
    walk.fireEvents(0.0f, 2.0f, log); // two cycles
    CHECK(fired == "footstep_l footstep_r sound:step footstep_l footstep_r sound:step ");
    fired.clear();
    walk.fireEvents(0.25f, 0.5f, log); // the event at `from` itself is not repeated
    CHECK(fired.empty());

    asset::ClipData land = armSwing("none/t_jump_land");
    land.events = {{1.0f, "land"}};
    const Clip once(land, s);
    fired.clear();
    once.fireEvents(0.9f, 3.0f, log); // one-shots fire once, also at their last frame
    CHECK(fired == "land ");
}

TEST_CASE("Blending: poses, masks, additive")
{
    const Skeleton s = Skeleton::create(threeBones()).value();
    Pose a = s.restPose();
    Pose b = s.restPose();
    b[2].rotation = glm::angleAxis(glm::radians(90.0f), Vec3(0, 0, 1));
    b[1].translation = Vec3(0, 3, 0);
    Pose mixed = a;
    blendPose(mixed, b, 0.5f);
    CHECK(armAngle(mixed) == doctest::Approx(45.0f).epsilon(0.01));
    CHECK(near(mixed[1].translation, Vec3(0, 2, 0)));
    Pose masked = a;
    const std::vector<f32> armOnly = s.maskBelow("arm");
    blendPose(masked, b, 1.0f, armOnly);
    CHECK(armAngle(masked) == doctest::Approx(90.0f).epsilon(0.01));
    CHECK(near(masked[1].translation, Vec3(0, 1, 0))); // spine untouched

    // Additive: b relative to a reference adds its change on top of another pose.
    Pose base = s.restPose();
    base[2].rotation = glm::angleAxis(glm::radians(30.0f), Vec3(0, 0, 1));
    Pose reference = s.restPose();
    addPose(base, b, reference, 1.0f);
    CHECK(armAngle(base) == doctest::Approx(120.0f).epsilon(0.01));
}

TEST_CASE("Animator: transitions, cross-fades, blend weights, end, any-state, root motion")
{
    const Skeleton s = Skeleton::create(threeBones()).value();
    asset::AnimationSetData set;
    set.clips.push_back(armSwing("none/s_idle"));
    set.clips.push_back(armSwing("none/s_run"));
    set.clips.back().duration = 2.0f;
    set.clips.back().tracks[0].times = {0.0f, 2.0f};
    set.clips.push_back(armSwing("none/t_jump"));
    asset::ClipData climb = armSwing("none/t_climb");
    climb.tracks.push_back({"root",
                            asset::TrackData::Path::Translation,
                            false,
                            {0.0f, 1.0f},
                            {Vec4(0, 0, 0, 0), Vec4(0, 1, 2, 0)}});
    set.clips.push_back(climb);

    auto graph = AnimGraph::parse(R"(
version = 1
sets = ["x.glb"]
start = "move"
[[state]]
name = "move"
blend = "speed"
points = [{ value = 0.0, clip = "none/s_idle" }, { value = 4.0, clip = "none/s_run" }]
[[state]]
name = "jump"
clip = "none/t_jump"
[[state]]
name = "climb"
clip = "none/t_climb"
root_motion = true
[[transition]]
from = "*"
to = "climb"
when = ["climb == 2"]
blend = 0
[[transition]]
from = "move"
to = "jump"
when = ["air", "speed >= 1"]
blend = 0.5
[[transition]]
from = "jump"
to = "move"
when = ["end"]
[[transition]]
from = "climb"
to = "move"
when = ["end"]
blend = 0
)",
                                  "test.animgraph.toml");
    REQUIRE_MESSAGE(graph.ok(), (graph.ok() ? "" : graph.error().message));
    const asset::AnimationSetData* sets[] = {&set};
    auto created = Animator::create(graph.value(), s, sets);
    REQUIRE_MESSAGE(created.ok(), (created.ok() ? "" : created.error().message));
    Animator a = std::move(created).value();
    CHECK(a.state() == "move");

    // Blend: speed 2 -> half idle, half run.
    a.setFloat("speed", 2.0f);
    a.update(0.1f);
    const auto clips = a.activeClips();
    REQUIRE(clips.size() == 2);
    CHECK(clips[0].weight == doctest::Approx(0.5f));
    CHECK(clips[1].weight == doctest::Approx(0.5f));
    // Shared phase: one cycle lasts 0.5 * 1 s + 0.5 * 2 s = 1.5 s.
    CHECK(a.stateProgress() == doctest::Approx(0.1f / 1.5f).epsilon(0.001));

    // A condition that does not hold yet, then the transition with a 0.5 s cross-fade.
    a.setBool("air", true);
    a.setFloat("speed", 0.5f);
    a.update(0.1f);
    CHECK(a.state() == "move");
    a.setFloat("speed", 1.0f);
    a.update(0.1f);
    CHECK(a.state() == "jump");
    CHECK(a.previousState() == "move");
    CHECK(a.fadeWeight() == doctest::Approx(0.2f).epsilon(0.01));
    for (int i = 0; i < 4; ++i)
    {
        a.update(0.1f);
    }
    CHECK(a.fadeWeight() == doctest::Approx(1.0f));
    a.update(0.6f); // the 1 s jump has played through ...
    CHECK(a.stateEnded());
    a.update(0.01f); // ... "end" leads back
    CHECK(a.state() == "move");

    // Any state -> climb; the root's movement is reported, the drawn root stays at rest.
    a.setFloat("climb", 2.0f);
    a.update(0.0f);
    CHECK(a.state() == "climb");
    a.setFloat("climb", 0.0f);
    Vec3 moved(0.0f);
    for (int i = 0; i < 10; ++i)
    {
        a.update(0.05f);
        moved += a.rootMotion();
        CHECK(near(a.pose()[0].translation, Vec3(0.0f)));
    }
    CHECK(near(moved, Vec3(0.0f, 0.5f, 1.0f)));

    // Events reach the callback.
    std::vector<std::string> events;
    set.clips[0].events = {{0.5f, "footstep_l"}};
    auto again = Animator::create(graph.value(), s, sets).value();
    again.update(0.6f, [&](std::string_view clip, std::string_view e)
                 { events.push_back(std::string(clip) + ":" + std::string(e)); });
    REQUIRE(events.size() == 1);
    CHECK(events[0] == "none/s_idle:footstep_l");

    // Overlay over the arm only, faded in.
    again.playOverlay("none/t_jump", "arm", 0.1f);
    again.update(0.1f);
    CHECK(again.activeClips().back().clip == "none/t_jump");
    CHECK(again.activeClips().back().weight == doctest::Approx(1.0f));
}

TEST_CASE("Animator: playback rate follows the clips' own speed, weighted in blends, within rate_range")
{
    const Skeleton s = Skeleton::create(threeBones()).value();
    asset::AnimationSetData set;
    set.clips.push_back(armSwing("none/s_idle")); // standing: no speed
    set.clips.push_back(armSwing("none/s_walk"));
    set.clips.back().speed = 1.0f;
    set.clips.push_back(armSwing("none/s_strafe_l"));
    set.clips.back().speed = 1.0f;
    set.clips.push_back(armSwing("none/s_back")); // no speed: rate 1 although the state asks for one
    const asset::AnimationSetData* sets[] = {&set};
    auto graph = AnimGraph::parse(R"(
version = 1
sets = ["x.glb"]
start = "move"
[[state]]
name = "move"
blend = "speed"
rate = "speed"
points = [{ value = 0.0, clip = "none/s_idle" }, { value = 1.6, clip = "none/s_walk" }]
[[state]]
name = "strafe"
clip = "none/s_strafe_l"
rate = "strafe"
[[state]]
name = "back"
clip = "none/s_back"
rate = "speed"
[[state]]
name = "plain"
clip = "none/s_walk"
)",
                                  "rate.toml");
    REQUIRE_MESSAGE(graph.ok(), (graph.ok() ? "" : graph.error().message));
    CHECK(graph.value().rateRange[0] == doctest::Approx(0.6f));
    CHECK(graph.value().rateRange[1] == doctest::Approx(1.8f));
    Animator a = Animator::create(graph.value(), s, sets).value();

    a.setFloat("speed", 1.6f); // walk alone, made for 1.0 m/s
    CHECK(a.playbackRate() == doctest::Approx(1.6f));
    a.setFloat("speed", 1.0f); // idle and walk blended: only walk has a speed
    CHECK(a.playbackRate() == doctest::Approx(1.0f));
    a.setFloat("speed", 0.2f); // limits
    CHECK(a.playbackRate() == doctest::Approx(0.6f));
    a.setFloat("speed", 0.0f); // idle alone: nothing to match
    CHECK(a.playbackRate() == doctest::Approx(1.0f));

    a.enter("strafe");
    a.setFloat("strafe", -1.5f); // left: the magnitude counts
    CHECK(a.playbackRate() == doctest::Approx(1.5f));
    a.update(0.5f);
    CHECK(a.stateTime() == doctest::Approx(0.75f));
    a.setFloat("strafe", 3.0f);
    CHECK(a.playbackRate() == doctest::Approx(1.8f));

    a.enter("back");
    CHECK(a.playbackRate() == doctest::Approx(1.0f));
    a.enter("plain");
    CHECK(a.playbackRate() == doctest::Approx(1.0f));

    auto narrow =
        AnimGraph::parse("version = 1\nsets = [\"a\"]\nrate_range = [0.8, 1.2]\n[[state]]\nname = \"x\"\n"
                         "clip = \"none/s_walk\"\nrate = \"speed\"\n",
                         "g");
    REQUIRE(narrow.ok());
    Animator b = Animator::create(narrow.value(), s, sets).value();
    b.setFloat("speed", 4.0f);
    CHECK(b.playbackRate() == doctest::Approx(1.2f));
}

TEST_CASE("AnimGraph: errors")
{
    const auto fails = [](const char* toml, const char* expected)
    {
        auto g = AnimGraph::parse(toml, "g.toml");
        REQUIRE_FALSE(g.ok());
        CHECK_MESSAGE(g.error().message.find(expected) != std::string::npos, g.error().message);
    };
    fails("sets = [\"a\"]\n", "version = 1");
    fails("version = 1\n", "sets");
    fails("version = 1\nsets = [\"a\"]\n", "[[state]]");
    fails("version = 1\nsets = [\"a\"]\nrate_range = [1.5, 1.0]\n", "rate_range");
    fails("version = 1\nsets = [\"a\"]\nrate_range = [0.0, 1.0]\n", "rate_range");
    fails("version = 1\nsets = [\"a\"]\n[[state]]\nname = \"x\"\n", "'clip'");
    fails("version = 1\nsets = [\"a\"]\n[[state]]\nname = \"x\"\nblend = \"s\"\n"
          "points = [{ value = 1.0, clip = \"a\" }, { value = 0.5, clip = \"b\" }]\n",
          "rise");
    fails("version = 1\nsets = [\"a\"]\n[[state]]\nname = \"x\"\nclip = \"a\"\n"
          "[[transition]]\nfrom = \"x\"\nto = \"x\"\nwhen = [\"speed >> 2\"]\n",
          "condition");

    const Skeleton s = Skeleton::create(threeBones()).value();
    asset::AnimationSetData set;
    set.clips.push_back(armSwing("none/s_idle"));
    const asset::AnimationSetData* sets[] = {&set};
    auto unknownClip = AnimGraph::parse(
        "version = 1\nsets = [\"a\"]\n[[state]]\nname = \"x\"\nclip = \"none/s_jog\"\n", "g");
    REQUIRE(unknownClip.ok());
    CHECK_FALSE(Animator::create(unknownClip.value(), s, sets).ok());
    auto unknownState =
        AnimGraph::parse("version = 1\nsets = [\"a\"]\n[[state]]\nname = \"x\"\nclip = \"none/s_idle\"\n"
                         "[[transition]]\nfrom = \"x\"\nto = \"y\"\n",
                         "g");
    REQUIRE(unknownState.ok());
    CHECK_FALSE(Animator::create(unknownState.value(), s, sets).ok());
}

TEST_CASE("Animator with the real data: reference rig, clip sets, human.animgraph.toml")
{
    const auto read = [](const char* relative)
    {
        auto bytes = fs::readFile(fs::fromUtf8(std::string(G7_ASSET_SOURCE_DIR) + "/" + relative));
        REQUIRE_MESSAGE(bytes.ok(), relative);
        return std::move(bytes).value();
    };
    auto rig = asset::loadSkinnedGltf(read("characters/rig/human_reference.glb"), {}, "rig");
    REQUIRE(rig.ok());
    const Skeleton skeleton = Skeleton::create(rig.value().skeleton).value();
    std::vector<asset::AnimationSetData> data;
    for (const char* set : {"characters/anims/human/none.glb", "characters/anims/human/swim.glb",
                            "characters/anims/human/dive.glb"})
    {
        auto loaded = asset::loadAnimationGltf(read(set), {}, set);
        REQUIRE(loaded.ok());
        data.push_back(std::move(loaded).value());
    }
    const std::vector<u8> graphText = read("data/anim/human.animgraph.toml");
    auto graph =
        AnimGraph::parse(std::string_view(reinterpret_cast<const char*>(graphText.data()), graphText.size()),
                         "human.animgraph.toml");
    REQUIRE_MESSAGE(graph.ok(), (graph.ok() ? "" : graph.error().message));
    CHECK(graph.value().sets.size() == 3);
    std::vector<const asset::AnimationSetData*> sets;
    for (const auto& d : data)
    {
        sets.push_back(&d);
    }
    auto created = Animator::create(graph.value(), skeleton, sets);
    REQUIRE_MESSAGE(created.ok(), (created.ok() ? "" : created.error().message));
    Animator a = std::move(created).value();

    CHECK(a.state() == "move");
    a.setFloat("speed", 4.0f);
    a.update(0.1f);
    REQUIRE(a.activeClips().size() == 1);
    CHECK(a.activeClips()[0].clip == "none/s_run");
    // Every bone transform is finite after sampling a real clip.
    for (const BoneTransform& b : a.pose())
    {
        CHECK(std::isfinite(b.translation.x + b.translation.y + b.translation.z));
        CHECK(std::isfinite(b.rotation.w));
    }
    a.setBool("jump", true);
    a.update(0.1f);
    CHECK(a.state() == "jump_run");
    a.setBool("jump", false);

    // Climbing the mid class: the root rises 1.6 m (built for that height), the drawn root stays.
    a.setBool("air", false);
    a.setFloat("climb", 2.0f);
    a.update(0.0f);
    CHECK(a.state() == "climb_mid");
    a.setFloat("climb", 0.0f);
    Vec3 moved(0.0f);
    for (int i = 0; i < 200 && a.state() == "climb_mid"; ++i)
    {
        a.update(1.0f / 60.0f);
        moved += a.rootMotion();
    }
    CHECK(moved.y == doctest::Approx(1.6f).epsilon(0.03));
    CHECK(a.state() == "move");

    a.setBool("swim", true);
    a.update(0.1f);
    CHECK(a.state() == "swim");
    a.setBool("dive", true);
    a.update(0.1f);
    CHECK(a.state() == "dive");
}
