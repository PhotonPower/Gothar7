// The player in the engine (M5 part C, label "gpu"): starts on the start point, walks with the movement
// data, stays on the ground, triggers notice it, the camera follows behind without going through walls.

#include "GlFixture.hpp"

#include <g7/physics/Character.hpp>
#include <g7/runtime/Engine.hpp>

#include <algorithm>
#include <cmath>
#include <utility>

using namespace g7;

namespace
{
EngineConfig playerConfig()
{
    EngineConfig config;
    config.appName = "player";
    config.window.size = {160, 90};
    config.window.vsync = false;
    config.world = fs::fromUtf8("testworld/camp.g7world");
    config.start = "START_LAGER"; // feet at (34, 0, 0), looking west (-X)
    config.shaderDirectory = fs::fromUtf8(G7_SHADER_DIR);
    config.fixedFrameSeconds = 1.0 / 60.0;
    g7::test::keepVideoAlive();
    return config;
}

/// The camera can see the head: nothing of the world lies between them.
bool cameraSeesTarget(Engine& engine, const Vec3& target)
{
    const Vec3 eye = engine.camera().transform.position;
    const Vec3 toTarget = target - eye;
    const f32 distance = glm::length(toTarget);
    return distance < 0.05f ||
           !engine.physics()
                .raycast(eye, toTarget / distance, distance - 0.05f, physics::layerBit(physics::Layer::World))
                .has_value();
}
} // namespace

TEST_CASE("Player GPU: starts on the start point with the camera behind and above")
{
    Engine engine(playerConfig());
    auto result = engine.init();
    REQUIRE_MESSAGE(result.ok(), (result.ok() ? "" : result.error().message));
    const physics::CharacterController* player = engine.player();
    REQUIRE(player != nullptr);
    CHECK(engine.playerCameraActive());
    CHECK(player->feet().x == doctest::Approx(34.0f));
    CHECK(glm::degrees(engine.playerMovement().yaw()) == doctest::Approx(90.0f).epsilon(0.01)); // west
    for (int i = 0; i < 30; ++i)
    {
        CHECK(engine.runFrame());
    }
    CHECK(player->state() == physics::MoveState::Ground);
    CHECK(std::abs(player->feet().y) < 0.05f); // the camp is flat at 0
    // Behind (east of) the figure, above its head, looking west and a little down.
    const Vec3 eye = engine.camera().transform.position;
    CHECK(eye.x > 34.0f + 2.0f);
    CHECK(eye.y > 1.55f);
    const Vec3 forward = engine.camera().transform.rotation * Vec3(0.0f, 0.0f, -1.0f);
    CHECK(forward.x < -0.9f);
    CHECK(forward.y < 0.0f);
    CHECK(engine.movementSettings().runSpeed == doctest::Approx(4.0f)); // data/movement.toml
    CHECK(engine.renderDevice()->debugErrorCount() == 0);
}

TEST_CASE("Player GPU: walks and runs at the speeds of the movement data, stops, the camera follows")
{
    Engine engine(playerConfig());
    auto result = engine.init();
    REQUIRE_MESSAGE(result.ok(), (result.ok() ? "" : result.error().message));
    const physics::CharacterController* player = engine.player();
    REQUIRE(player != nullptr);

    gameplay::MoveInput walk;
    walk.forward = 1.0f;
    walk.walk = true;
    engine.setPlayerInputOverride(walk);
    for (int i = 0; i < 60; ++i) // 1 s walking: 1.6 m less the acceleration
    {
        CHECK(engine.runFrame());
    }
    const f32 walked = 34.0f - player->feet().x;
    CHECK(walked > 1.4f);
    CHECK(walked < 1.6f);

    gameplay::MoveInput run;
    run.forward = 1.0f;
    engine.setPlayerInputOverride(run);
    const f32 before = player->feet().x;
    for (int i = 0; i < 60; ++i) // 1 s running: 4 m less the acceleration from 1.6 m/s
    {
        CHECK(engine.runFrame());
    }
    const f32 ran = before - player->feet().x;
    CHECK(ran > 3.6f);
    CHECK(ran < 4.0f);
    CHECK(std::abs(player->feet().z) < 0.01f); // straight on
    CHECK(player->state() == physics::MoveState::Ground);
    CHECK(engine.camera().transform.position.x > player->feet().x + 1.0f);
    CHECK(cameraSeesTarget(engine, player->feet() + Vec3(0.0f, 1.55f, 0.0f)));

    // Stop: braking within a few frames.
    engine.setPlayerInputOverride(gameplay::MoveInput{});
    for (int i = 0; i < 30; ++i)
    {
        CHECK(engine.runFrame());
    }
    const Vec3 stopped = player->feet();
    CHECK(engine.runFrame());
    CHECK(glm::length(player->feet() - stopped) < 0.001f);
    CHECK(engine.renderDevice()->debugErrorCount() == 0);
}

TEST_CASE("Player GPU: walking into a level change trigger takes the player to the cave")
{
    EngineConfig config = playerConfig();
    // (-26, 0, 20), looking east into the camp; TRG_TO_CAVE lies behind it at x -30.
    config.start = "START_LAGER_HOEHLE";
    Engine engine(std::move(config));
    auto result = engine.init();
    REQUIRE_MESSAGE(result.ok(), (result.ok() ? "" : result.error().message));
    REQUIRE(engine.player() != nullptr);
    gameplay::MoveInput back;
    back.forward = -1.0f; // backwards, 1.4 m/s
    engine.setPlayerInputOverride(back);
    for (int i = 0; i < 240 && engine.worldPath() != "testworld/cave.g7world"; ++i)
    {
        CHECK(engine.runFrame());
    }
    CHECK(engine.worldPath() == "testworld/cave.g7world");
    // The figure is no instance of a world: the level change must not release it (it was, see #99).
    CHECK(engine.model("characters/figures/placeholder_mannequin.glb") != nullptr);
    // A new player on the cave's start point; the override still holds, so stop first.
    engine.setPlayerInputOverride(gameplay::MoveInput{});
    REQUIRE(engine.player() != nullptr);
    for (int i = 0; i < 20; ++i) // spawned 5 cm above its start point: settles
    {
        CHECK(engine.runFrame());
    }
    CHECK(engine.player()->state() == physics::MoveState::Ground);
    CHECK(engine.player()->feet().z == doctest::Approx(5.0f).epsilon(0.01)); // START_HOEHLE
    CHECK(engine.renderDevice()->debugErrorCount() == 0);
}

TEST_CASE("Player GPU: turning walks into the camp's huts; the camera stays in front of walls")
{
    Engine engine(playerConfig());
    auto result = engine.init();
    REQUIRE_MESSAGE(result.ok(), (result.ok() ? "" : result.error().message));
    const physics::CharacterController* player = engine.player();
    REQUIRE(player != nullptr);
    // Run around for a while with changing turns: whatever it meets, it neither falls through the ground
    // nor ends up inside a wall, and the camera always sees the figure.
    gameplay::MoveInput input;
    input.forward = 1.0f;
    for (int i = 0; i < 600; ++i)
    {
        input.turn = (i / 90) % 2 == 0 ? 0.25f : -0.35f;
        engine.setPlayerInputOverride(input);
        REQUIRE(engine.runFrame());
        CHECK(player->feet().y > -0.5f);
        if (i % 30 == 0)
        {
            CHECK(cameraSeesTarget(engine, player->feet() + Vec3(0.0f, 1.55f, 0.0f)));
        }
    }
    engine.setDebugOverlay(true); // capsule, ground normal and speed drawn without GL errors
    CHECK(engine.runFrame());
    CHECK(engine.renderDevice()->debugErrorCount() == 0);
}

namespace
{
/// Highest feet height during `frames` frames.
f32 highest(Engine& engine, int frames)
{
    f32 top = -1e9f;
    for (int i = 0; i < frames; ++i)
    {
        REQUIRE(engine.runFrame());
        top = std::max(top, engine.player()->feet().y);
    }
    return top;
}

constexpr f32 kFacingSouth = glm::pi<f32>(); // +Z (the climbing course lies south of its start point)
} // namespace

TEST_CASE("Player GPU: jumps higher from a run, lands, waits before the next jump")
{
    EngineConfig config = playerConfig();
    config.start = "START_KLETTERPLATZ"; // (46, 0, 2), the climbing course in the camp
    Engine engine(std::move(config));
    auto result = engine.init();
    REQUIRE_MESSAGE(result.ok(), (result.ok() ? "" : result.error().message));
    REQUIRE(engine.player() != nullptr);
    engine.teleportPlayer(Vec3(46.0f, 0.02f, -6.0f), 0.0f); // open ground, looking north
    CHECK(highest(engine, 20) < 0.05f);

    gameplay::MoveInput jump;
    jump.jump = true;
    engine.setPlayerInputOverride(jump);
    CHECK(highest(engine, 60) == doctest::Approx(0.9f).epsilon(0.06)); // from standing
    CHECK(engine.player()->state() == physics::MoveState::Ground);
    CHECK(engine.lastFallDamage() == doctest::Approx(0.0f));

    gameplay::MoveInput run;
    run.forward = 1.0f;
    engine.setPlayerInputOverride(run);
    highest(engine, 60); // up to running speed
    const f32 start = engine.player()->feet().z;
    run.jump = true;
    engine.setPlayerInputOverride(run);
    CHECK(highest(engine, 70) == doctest::Approx(1.1f).epsilon(0.06)); // from a run
    CHECK(start - engine.player()->feet().z > 4.0f);                   // and far
    CHECK(engine.renderDevice()->debugErrorCount() == 0);
}

TEST_CASE("Player GPU: climbs the three ledge classes, not the block above them")
{
    EngineConfig config = playerConfig();
    config.start = "START_KLETTERPLATZ";
    Engine engine(std::move(config));
    auto result = engine.init();
    REQUIRE_MESSAGE(result.ok(), (result.ok() ? "" : result.error().message));
    REQUIRE(engine.player() != nullptr);
    // KLETTER_EINZEL_1..4: 2 x 2 m blocks at x 40/44/48/52, z 5..7, 0.9/1.5/2.1/2.6 m high.
    const std::pair<f32, f32> blocks[] = {{40.0f, 0.9f}, {44.0f, 1.5f}, {48.0f, 2.1f}, {52.0f, 2.6f}};
    for (const auto& [x, height] : blocks)
    {
        CAPTURE(height);
        engine.teleportPlayer(Vec3(x, 0.02f, 4.4f), kFacingSouth); // 0.3 m before the wall
        engine.setPlayerInputOverride(gameplay::MoveInput{});
        highest(engine, 20); // settles; the 0.2 s jump lock after landing passes
        gameplay::MoveInput jump;
        jump.jump = true;
        engine.setPlayerInputOverride(jump);
        CHECK(engine.runFrame());
        const bool climbable = height < 2.2f;
        CHECK(engine.playerClimbing() == climbable);
        highest(engine, 120); // the longest climb takes 1.4 s
        CHECK_FALSE(engine.playerClimbing());
        if (climbable)
        {
            CHECK(engine.player()->feet().y == doctest::Approx(height).epsilon(0.03));
            CHECK(engine.player()->feet().z > 5.3f); // standing on it
            CHECK(engine.player()->state() == physics::MoveState::Ground);
        }
        else
        {
            CHECK(engine.player()->feet().y < 0.05f); // a jump against the wall instead
        }
    }
    CHECK(engine.renderDevice()->debugErrorCount() == 0);
}

TEST_CASE("Player GPU: a 6 m fall from the top of the stairs costs 20 hit points")
{
    EngineConfig config = playerConfig();
    config.start = "START_KLETTERPLATZ";
    Engine engine(std::move(config));
    auto result = engine.init();
    REQUIRE_MESSAGE(result.ok(), (result.ok() ? "" : result.error().message));
    REQUIRE(engine.player() != nullptr);
    // KLETTER_TREPPE_6: x 49..51, z 11..13, 6 m high. Walk off its south side.
    engine.teleportPlayer(Vec3(50.0f, 6.02f, 12.0f), kFacingSouth);
    highest(engine, 10);
    CHECK(engine.lastFallDamage() == doctest::Approx(0.0f));
    gameplay::MoveInput walk;
    walk.forward = 1.0f;
    walk.walk = true;
    engine.setPlayerInputOverride(walk);
    highest(engine, 180);
    CHECK(engine.player()->feet().y < 0.05f);
    CHECK(engine.lastFallDamage() == doctest::Approx(20.0f).epsilon(0.05));
}
