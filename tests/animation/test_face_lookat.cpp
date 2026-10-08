// Face and look-at (M6 part D): blinking, expressions, talking; neck and head turning towards a target
// within limits and at limited speed; [face] / [look_at] in the graph file.

#include <g7/animation/Animator.hpp>
#include <g7/animation/Face.hpp>
#include <g7/animation/LookAt.hpp>
#include <g7/animation/Skeleton.hpp>

#include <doctest/doctest.h>

#include <cmath>
#include <optional>
#include <ostream> // doctest needs it to print std::string operands
#include <string>

using namespace g7;
using namespace g7::animation;

namespace
{
/// root - spine (1 m up) - neck (0.5 m up) - head (0.1 m up). Faces +Z like the reference rig.
Skeleton headSkeleton()
{
    asset::SkeletonData s;
    s.names = {"root", "spine", "neck", "head"};
    s.parents = {-1, 0, 1, 2};
    s.translations = {Vec3(0.0f), Vec3(0, 1, 0), Vec3(0, 0.5f, 0), Vec3(0, 0.1f, 0)};
    s.rotations.assign(4, Quat(1, 0, 0, 0));
    s.scales.assign(4, Vec3(1.0f));
    return Skeleton::create(s).value();
}

/// Where the head looks (+Z of its model-space rotation), as yaw and pitch in degrees.
std::pair<f32, f32> headAngles(const Skeleton& skeleton, const Pose& pose)
{
    std::vector<Mat4> m(skeleton.size());
    skeleton.modelSpace(pose, m);
    const Vec3 forward = glm::normalize(Vec3(m[3] * Vec4(0, 0, 1, 0)));
    return {glm::degrees(std::atan2(forward.x, forward.z)),
            glm::degrees(std::atan2(forward.y, std::sqrt(forward.x * forward.x + forward.z * forward.z)))};
}

f32 run(FaceAnimator& face, f32 seconds, FaceMorph morph, f32 step = 1.0f / 60.0f)
{
    f32 highest = 0.0f;
    for (f32 t = 0.0f; t < seconds; t += step)
    {
        face.update(step);
        highest = std::max(highest, face.weight(morph));
    }
    return highest;
}
} // namespace

TEST_CASE("Face: morph names in contract order, blinking, expressions, talking")
{
    CHECK(faceMorphName(FaceMorph::VisAa) == "vis_aa");
    CHECK(faceMorphName(FaceMorph::BlinkR) == "blink_r");
    CHECK(faceMorphName(FaceMorph::ExprSleep) == "expr_sleep");
    CHECK(kFaceMorphCount == 15);

    FaceSettings settings;
    settings.blinkMinSeconds = 1.0f;
    settings.blinkMaxSeconds = 2.0f;
    FaceAnimator face(7, settings);
    REQUIRE(face.weights().size() == 15);
    // A blink within 2 s, both eyes, fully closed for a moment, then open again.
    CHECK(run(face, 2.2f, FaceMorph::BlinkL) > 0.8f); // sampled at 60 Hz
    CHECK(face.weight(FaceMorph::BlinkL) == face.weight(FaceMorph::BlinkR));
    // At most one blink per 1 s (closing + opening take 0.15 s).
    int blinks = 0;
    bool closed = false;
    for (int i = 0; i < 600; ++i) // 10 s
    {
        face.update(1.0f / 60.0f);
        const bool now = face.weight(FaceMorph::BlinkL) > 0.5f;
        blinks += now && !closed ? 1 : 0;
        closed = now;
    }
    CHECK(blinks >= 5);
    CHECK(blinks <= 10);

    // Expressions fade in over expression_seconds, others fade out.
    CHECK_FALSE(face.setExpression("bored"));
    CHECK(face.setExpression("angry", 0.8f));
    CHECK(face.expression() == "angry");
    face.update(0.15f);
    CHECK(face.weight(FaceMorph::ExprAngry) == doctest::Approx(0.5f));
    face.update(0.3f);
    CHECK(face.weight(FaceMorph::ExprAngry) == doctest::Approx(0.8f));
    CHECK(face.setExpression("friendly"));
    face.update(0.3f);
    CHECK(face.weight(FaceMorph::ExprAngry) == doctest::Approx(0.0f));
    CHECK(face.weight(FaceMorph::ExprFriendly) == doctest::Approx(1.0f));
    CHECK(face.setExpression(""));
    face.update(0.3f);
    CHECK(face.weight(FaceMorph::ExprFriendly) == doctest::Approx(0.0f));
    CHECK(face.expression().empty());

    // Asleep: no blinking.
    face.setExpression("sleep");
    face.update(0.5f);
    CHECK(run(face, 5.0f, FaceMorph::BlinkL) == 0.0f);
    face.setExpression("");

    // Talking moves the mouth (some viseme open), silence closes it.
    face.setTalking(true);
    f32 mouth = 0.0f;
    for (int i = 0; i < 60; ++i)
    {
        face.update(1.0f / 60.0f);
        for (usize v = 0; v < 8; ++v)
        {
            mouth = std::max(mouth, face.weights()[v]);
        }
    }
    CHECK(mouth > 0.4f);
    face.setTalking(false);
    face.update(0.3f);
    for (usize v = 0; v < 8; ++v)
    {
        CHECK(face.weights()[v] == doctest::Approx(0.0f));
    }

    // Lip sync from loudness (M13 D): the voice opens vis_aa (as far as talkWeight), smoothed; closed again
    // when it is quiet; nullopt hands the mouth back to talking.
    face.setTalking(true);
    face.setMouthOpen(1.0f);
    CHECK(face.mouthDriven());
    for (int i = 0; i < 12; ++i)
    {
        face.update(1.0f / 60.0f);
    }
    CHECK(face.weight(FaceMorph::VisAa) == doctest::Approx(FaceSettings{}.talkWeight).epsilon(0.05));
    CHECK(face.weight(FaceMorph::VisEe) == 0.0f); // no random shapes while a voice drives the mouth
    face.setMouthOpen(0.0f);
    for (int i = 0; i < 12; ++i)
    {
        face.update(1.0f / 60.0f);
    }
    CHECK(face.weight(FaceMorph::VisAa) < 0.05f);
    face.setMouthOpen(std::nullopt);
    CHECK_FALSE(face.mouthDriven());
}

TEST_CASE("LookAt: turns neck and head towards the target within limits, at limited speed")
{
    const Skeleton skeleton = headSkeleton();
    CHECK_FALSE(LookAt::create(skeleton, LookAtSettings{{{"neck_01", 1.0f}}}).ok());
    LookAtSettings settings; // neck 40 %, head 60 %, yaw 70, pitch 35, 240 deg/s
    LookAt look = LookAt::create(skeleton, settings).value();
    const auto settle = [&](int steps)
    {
        Pose pose = skeleton.restPose();
        for (int i = 0; i < steps; ++i)
        {
            pose = skeleton.restPose();
            look.update(1.0f / 60.0f, skeleton, pose);
        }
        return pose;
    };

    // 45 deg to the left (+X), level with the head (1.6 m).
    look.setTarget(Vec3(2.0f, 1.6f, 2.0f));
    Pose pose = settle(1);
    CHECK(look.yawDegrees() == doctest::Approx(4.0f)); // 240 deg/s for 1/60 s
    pose = settle(60);
    auto [yaw, pitch] = headAngles(skeleton, pose);
    CHECK(yaw == doctest::Approx(45.0f).epsilon(0.01));
    CHECK(pitch == doctest::Approx(0.0f).scale(1.0f));
    // The neck carries 40 % of the turn.
    std::vector<Mat4> m(skeleton.size());
    skeleton.modelSpace(pose, m);
    const Vec3 neckForward = glm::normalize(Vec3(m[2] * Vec4(0, 0, 1, 0)));
    CHECK(glm::degrees(std::atan2(neckForward.x, neckForward.z)) == doctest::Approx(18.0f).epsilon(0.01));

    // Up: pitch, clamped at 35 deg.
    look.setTarget(Vec3(0.0f, 4.6f, 1.0f));
    std::tie(yaw, pitch) = headAngles(skeleton, settle(60));
    CHECK(pitch == doctest::Approx(35.0f).epsilon(0.01));
    CHECK(yaw == doctest::Approx(0.0f).scale(1.0f));

    // Far to the right: clamped at -70; behind: back to the middle.
    look.setTarget(Vec3(-3.0f, 1.6f, 0.3f));
    std::tie(yaw, pitch) = headAngles(skeleton, settle(60));
    CHECK(yaw == doctest::Approx(-70.0f).epsilon(0.01));
    look.setTarget(Vec3(0.0f, 1.6f, -3.0f));
    std::tie(yaw, pitch) = headAngles(skeleton, settle(60));
    CHECK(yaw == doctest::Approx(0.0f).scale(1.0f));
    // No target: straight ahead.
    look.setTarget(Vec3(2.0f, 1.6f, 2.0f));
    settle(60);
    look.setTarget(std::nullopt);
    std::tie(yaw, pitch) = headAngles(skeleton, settle(60));
    CHECK(yaw == doctest::Approx(0.0f).scale(1.0f));
}

TEST_CASE("AnimGraph: [face] and [look_at]")
{
    auto graph = AnimGraph::parse(R"(
version = 1
sets = ["x.glb"]
[face]
blink_min = 3.0
blink_max = 4.0
talk_weight = 0.5
[look_at]
bones = ["spine", "head"]
shares = [0.3, 0.7]
max_yaw = 60.0
[[state]]
name = "x"
clip = "a"
)",
                                  "g.toml");
    REQUIRE_MESSAGE(graph.ok(), (graph.ok() ? "" : graph.error().message));
    CHECK(graph.value().face.blinkMinSeconds == doctest::Approx(3.0f));
    CHECK(graph.value().face.talkWeight == doctest::Approx(0.5f));
    CHECK(graph.value().face.blinkSeconds == doctest::Approx(0.15f)); // default
    REQUIRE(graph.value().lookAt.bones.size() == 2);
    CHECK(graph.value().lookAt.bones[0].first == "spine");
    CHECK(graph.value().lookAt.bones[1].second == doctest::Approx(0.7f));
    CHECK(graph.value().lookAt.maxYawDegrees == doctest::Approx(60.0f));
    CHECK(graph.value().lookAt.maxPitchDegrees == doctest::Approx(35.0f));

    const auto fails = [](const char* toml, const char* expected)
    {
        auto g = AnimGraph::parse(toml, "g.toml");
        REQUIRE_FALSE(g.ok());
        CHECK_MESSAGE(g.error().message.find(expected) != std::string::npos, g.error().message);
    };
    fails("version = 1\nsets = [\"a\"]\n[face]\nblink_min = 5.0\nblink_max = 2.0\n", "[face]");
    fails("version = 1\nsets = [\"a\"]\n[look_at]\nbones = [\"head\"]\nshares = []\n", "[look_at]");
}
