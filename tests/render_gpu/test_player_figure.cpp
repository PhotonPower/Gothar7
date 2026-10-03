// The hero's figure beyond the state machine (M6 part D, label "gpu"): things held at sockets, the head
// turning towards a target, blinking, expressions and talking.

#include "GlFixture.hpp"

#include <g7/animation/Face.hpp>
#include <g7/asset/Procedural.hpp>
#include <g7/runtime/Engine.hpp>

#include <algorithm>
#include <cmath>

using namespace g7;

namespace
{
EngineConfig figureConfig()
{
    EngineConfig config;
    config.appName = "figure";
    config.window.size = {160, 90};
    config.window.vsync = false;
    config.world = fs::fromUtf8("testworld/camp.g7world");
    config.start = "START_LAGER"; // feet at (34, 0, 0), looking west (-X)
    config.shaderDirectory = fs::fromUtf8(G7_SHADER_DIR);
    config.fixedFrameSeconds = 1.0 / 60.0;
    g7::test::keepVideoAlive();
    return config;
}

f32 face(Engine& engine, animation::FaceMorph morph)
{
    return engine.playerFaceWeights()[static_cast<usize>(morph)];
}
} // namespace

TEST_CASE("Player figure GPU: holds a stick in the hand while running, looks at a target, blinks, talks")
{
    Engine engine(figureConfig());
    auto result = engine.init();
    REQUIRE_MESSAGE(result.ok(), (result.ok() ? "" : result.error().message));
    REQUIRE(engine.player() != nullptr);
    REQUIRE_FALSE(engine.playerFigurePath().empty());

    // Sockets: the reference rig's, nothing else; a missing model is an error.
    const asset::MeshData stick = asset::makeBox(Vec3(0.015f, 0.45f, 0.015f), Vec4(0.5f, 0.3f, 0.1f, 1.0f));
    CHECK(engine.attachToPlayer("socket_hand_r", stick, "stick").ok());
    CHECK_FALSE(engine.attachToPlayer("socket_tail", stick, "stick").ok());
    CHECK_FALSE(engine.attachToPlayer("socket_back_2h", "models/does_not_exist.glb").ok());
    CHECK_FALSE(engine.playerSocketTransform("socket_tail").has_value());

    // Running: the hand swings back and forth beside the body, at hip to chest height.
    gameplay::MoveInput run;
    run.forward = 1.0f;
    engine.setPlayerInputOverride(run);
    for (int i = 0; i < 40; ++i)
    {
        CHECK(engine.runFrame());
    }
    f32 lowest = 1e9f;
    f32 highest = -1e9f;
    for (int i = 0; i < 40; ++i)
    {
        CHECK(engine.runFrame());
        const auto hand = engine.playerSocketTransform("socket_hand_r");
        REQUIRE(hand.has_value());
        const Vec3 at(hand.value()[3]);
        const Vec3 feet = engine.player()->feet();
        CHECK(glm::length(Vec3(at.x - feet.x, 0.0f, at.z - feet.z)) < 0.8f);
        // Along the run (-X) relative to the feet: how far forward the hand is.
        const f32 ahead = feet.x - at.x;
        lowest = std::min(lowest, ahead);
        highest = std::max(highest, ahead);
        CHECK(at.y - feet.y > 0.5f);
        CHECK(at.y - feet.y < 1.6f);
    }
    CHECK(highest - lowest > 0.15f); // it swings
    engine.detachFromPlayer("socket_hand_r");
    engine.setPlayerInputOverride(gameplay::MoveInput{});
    for (int i = 0; i < 40; ++i)
    {
        CHECK(engine.runFrame());
    }

    // Look-at: a point 3 m to the figure's right (north, -Z while it faces west), at head height.
    const Vec3 feet = engine.player()->feet();
    engine.setPlayerLookTarget(feet + Vec3(0.0f, 1.6f, -3.0f));
    for (int i = 0; i < 60; ++i)
    {
        CHECK(engine.runFrame());
    }
    CHECK(engine.playerLookYawDegrees() < -60.0f); // towards the right, up to the limit (70)
    engine.setPlayerLookTarget(std::nullopt);
    for (int i = 0; i < 60; ++i)
    {
        CHECK(engine.runFrame());
    }
    CHECK(engine.playerLookYawDegrees() == doctest::Approx(0.0f));

    // Face: an expression fades in, talking opens the mouth, the eyes blink within 6 s.
    REQUIRE(engine.playerFaceWeights().size() == animation::kFaceMorphCount);
    CHECK_FALSE(engine.setPlayerExpression("bored"));
    CHECK(engine.setPlayerExpression("friendly"));
    engine.setPlayerTalking(true);
    f32 mouth = 0.0f;
    f32 blink = 0.0f;
    for (int i = 0; i < 360; ++i)
    {
        CHECK(engine.runFrame());
        mouth = std::max(mouth, face(engine, animation::FaceMorph::VisAa) +
                                    face(engine, animation::FaceMorph::VisOh) +
                                    face(engine, animation::FaceMorph::VisEe));
        blink = std::max(blink, face(engine, animation::FaceMorph::BlinkL));
    }
    CHECK(face(engine, animation::FaceMorph::ExprFriendly) == doctest::Approx(1.0f));
    CHECK(mouth > 0.3f);
    CHECK(blink > 0.8f);
    CHECK(engine.renderDevice()->debugErrorCount() == 0);
}
