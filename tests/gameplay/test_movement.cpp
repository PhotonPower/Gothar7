// Player movement and third-person camera (M5 part C): gaits, turning, acceleration, camera lag and
// collision, movement.toml.

#include <g7/gameplay/Movement.hpp>

#include <doctest/doctest.h>

#include <cmath>
#include <ostream> // doctest needs it to print std::string operands
#include <string>

using namespace g7;
using namespace g7::gameplay;

namespace
{
bool near(const Vec3& a, const Vec3& b, f32 eps = 1e-3f)
{
    return glm::length(a - b) < eps;
}
} // namespace

TEST_CASE("Movement: gaits, backwards, sideways and diagonals")
{
    const MovementSettings s;
    MoveInput in;
    in.forward = 1.0f;
    CHECK(near(PlayerMovement::wantedVelocity(in, 0.0f, s), Vec3(0, 0, -4.0f))); // runs by default, -Z ahead
    in.walk = true;
    CHECK(glm::length(PlayerMovement::wantedVelocity(in, 0.0f, s)) == doctest::Approx(1.6f));
    in.sneak = true; // sneak wins
    CHECK(glm::length(PlayerMovement::wantedVelocity(in, 0.0f, s)) == doctest::Approx(1.1f));
    in = {};
    in.forward = -1.0f;
    CHECK(near(PlayerMovement::wantedVelocity(in, 0.0f, s), Vec3(0, 0, 1.4f)));
    in.sneak = true; // backwards no faster than sneaking
    CHECK(glm::length(PlayerMovement::wantedVelocity(in, 0.0f, s)) == doctest::Approx(1.1f));
    in = {};
    in.strafe = 1.0f;
    CHECK(near(PlayerMovement::wantedVelocity(in, 0.0f, s), Vec3(2.0f, 0, 0))); // right = +X when facing -Z
    in.forward = 1.0f; // diagonal: no faster than running
    const Vec3 diagonal = PlayerMovement::wantedVelocity(in, 0.0f, s);
    CHECK(glm::length(diagonal) == doctest::Approx(4.0f));
    CHECK(diagonal.x > 0.0f);
    CHECK(diagonal.z < 0.0f);
    // Facing left (yaw +90 degrees): forward is -X.
    in = {};
    in.forward = 1.0f;
    CHECK(near(PlayerMovement::wantedVelocity(in, glm::radians(90.0f), s), Vec3(-4.0f, 0, 0)));
    CHECK(near(PlayerMovement::wantedVelocity({}, 0.0f, s), Vec3(0.0f)));
}

TEST_CASE("Movement: turning with keys and mouse, acceleration and braking")
{
    const MovementSettings s;
    PlayerMovement m;
    MoveInput in;
    in.turn = 1.0f; // turn right for half a second: -90 degrees
    for (int i = 0; i < 30; ++i)
    {
        m.step(in, 1.0f / 60.0f, s);
    }
    CHECK(glm::degrees(m.yaw()) == doctest::Approx(-90.0f).epsilon(0.01));
    in = {};
    in.mouseTurn = -600.0f; // 600 px to the left at 0.15 deg/px: +90
    m.step(in, 1.0f / 60.0f, s);
    CHECK(std::abs(m.yaw()) < 1e-4f);
    m.reset(glm::radians(370.0f)); // wrapped
    CHECK(glm::degrees(m.yaw()) == doctest::Approx(10.0f).epsilon(0.001));
    m.reset(0.0f);

    in = {};
    in.forward = 1.0f;
    m.step(in, 0.1f, s); // 12 m/s^2 * 0.1 s
    CHECK(glm::length(m.velocity()) == doctest::Approx(1.2f).epsilon(0.001));
    for (int i = 0; i < 10; ++i)
    {
        m.step(in, 0.1f, s);
    }
    CHECK(glm::length(m.velocity()) == doctest::Approx(4.0f)); // capped at the run speed
    m.step({}, 0.1f, s);                                       // 16 m/s^2 braking
    CHECK(glm::length(m.velocity()) == doctest::Approx(2.4f).epsilon(0.001));
    m.step({}, 1.0f, s);
    CHECK(near(m.velocity(), Vec3(0.0f)));
}

TEST_CASE("Camera: behind and above the target, follows with lag, no overshoot")
{
    const CameraSettings s;
    ThirdPersonCamera cam;
    cam.reset(Vec3(0.0f), 0.0f, s);
    // Facing -Z: the camera is behind (+Z), above the target, 3 m away, looking down 12 degrees.
    CHECK(cam.target().y == doctest::Approx(1.55f));
    CHECK(cam.position().z == doctest::Approx(3.0f * std::cos(glm::radians(12.0f))).epsilon(0.001));
    CHECK(cam.position().y == doctest::Approx(1.55f + 3.0f * std::sin(glm::radians(12.0f))).epsilon(0.001));
    CHECK(glm::length(cam.position() - cam.target()) == doctest::Approx(3.0f));
    const Vec3 looking = cam.rotation() * Vec3(0, 0, -1);
    CHECK(near(looking, glm::normalize(cam.target() - cam.position())));

    // The figure jumps 10 m ahead: the camera follows smoothly, never past it.
    const Vec3 feet(0.0f, 0.0f, -10.0f);
    f32 previous = cam.target().z;
    for (int i = 0; i < 120; ++i)
    {
        cam.update(1.0f / 60.0f, feet, 0.0f, 0.0f, s, {});
        CHECK(cam.target().z <= previous + 1e-5f); // monotonic
        CHECK(cam.target().z >= -10.0f - 1e-4f);   // no overshoot
        previous = cam.target().z;
    }
    CHECK(cam.target().z == doctest::Approx(-10.0f).epsilon(0.001)); // 2 s = 16 time constants

    // Turning: the camera swings behind the new facing the short way round (179 -> -179 degrees).
    cam.reset(Vec3(0.0f), glm::radians(179.0f), s);
    for (int i = 0; i < 300; ++i)
    {
        cam.update(1.0f / 60.0f, Vec3(0.0f), glm::radians(-179.0f), 0.0f, s, {});
        CHECK(std::abs(std::abs(glm::degrees(cam.yaw())) - 179.0f) < 2.1f);
    }

    // Mouse tilt is clamped.
    cam.update(1.0f / 60.0f, Vec3(0.0f), 0.0f, 10000.0f, s, {});
    CHECK(cam.pitchDegrees() == doctest::Approx(60.0f));
    cam.update(1.0f / 60.0f, Vec3(0.0f), 0.0f, -10000.0f, s, {});
    CHECK(cam.pitchDegrees() == doctest::Approx(-40.0f));
}

TEST_CASE("Camera: moves closer in front of walls, never closer than the minimum")
{
    const CameraSettings s;
    ThirdPersonCamera cam;
    cam.reset(Vec3(0.0f), 0.0f, s);
    f32 askedRadius = 0.0f;
    // A wall 1.5 m behind the target.
    const ThirdPersonCamera::Obstruction wall = [&](const Vec3&, const Vec3&, f32 radius, f32 maxDistance)
    {
        askedRadius = radius;
        return maxDistance > 1.5f ? std::optional<f32>(1.5f) : std::nullopt;
    };
    cam.update(1.0f / 60.0f, Vec3(0.0f), 0.0f, 0.0f, s, wall);
    CHECK(askedRadius == doctest::Approx(0.2f));
    CHECK(cam.distance() == doctest::Approx(1.5f));
    CHECK(glm::length(cam.position() - cam.target()) == doctest::Approx(1.5f));
    const ThirdPersonCamera::Obstruction close = [](const Vec3&, const Vec3&, f32, f32) { return 0.1f; };
    cam.update(1.0f / 60.0f, Vec3(0.0f), 0.0f, 0.0f, s, close);
    CHECK(cam.distance() == doctest::Approx(0.6f));
    cam.update(1.0f / 60.0f, Vec3(0.0f), 0.0f, 0.0f, s, {});
    CHECK(cam.distance() == doctest::Approx(3.0f));
}

TEST_CASE("movement.toml: values, defaults and errors")
{
    auto parsed = MovementSettings::parse(R"(
version = 1
[speed]
run = 5
walk = 1.5
[camera]
distance = 4.0
pitch_degrees = -5
)",
                                          "movement.toml");
    REQUIRE_MESSAGE(parsed, (parsed ? "" : parsed.error().message));
    CHECK(parsed.value().runSpeed == doctest::Approx(5.0f)); // integers are fine
    CHECK(parsed.value().walkSpeed == doctest::Approx(1.5f));
    CHECK(parsed.value().sneakSpeed == doctest::Approx(1.1f)); // default kept
    CHECK(parsed.value().camera.distance == doctest::Approx(4.0f));
    CHECK(parsed.value().camera.pitchDegrees == doctest::Approx(-5.0f));

    const auto fails = [](const char* toml, const char* expected)
    {
        auto r = MovementSettings::parse(toml, "movement.toml");
        REQUIRE_FALSE(r);
        CHECK_MESSAGE(r.error().message.find(expected) != std::string::npos, r.error().message);
    };
    fails("[speed]\nrun = -1\n", "speed.run");
    fails("[speed]\nrun = \"fast\"\n", "speed.run");
    fails("[ground]\nmax_slope_degrees = 90\n", "max_slope");
    fails("[camera]\npitch_degrees = 70\n", "pitch");
    fails("[camera]\nmin_distance = 5\n", "min_distance");
    fails("[speed\n", "movement.toml");
}

TEST_CASE("Jump, fall damage, ledge classes and the climb path")
{
    CHECK(jumpSpeed(0.9f) == doctest::Approx(std::sqrt(2.0f * 9.81f * 0.9f)));
    CHECK(jumpSpeed(-1.0f) == doctest::Approx(0.0f));
    const FallSettings fall;
    CHECK(fallDamage(3.9f, fall) == doctest::Approx(0.0f));
    CHECK(fallDamage(4.0f, fall) == doctest::Approx(0.0f));
    CHECK(fallDamage(7.0f, fall) == doctest::Approx(30.0f)); // the city wall from outside
    const ClimbSettings climb;
    CHECK(classifyLedge(0.5f, climb) == LedgeClass::Low);
    CHECK(classifyLedge(1.0f, climb) == LedgeClass::Low);
    CHECK(classifyLedge(1.3f, climb) == LedgeClass::Mid);
    CHECK(classifyLedge(2.2f, climb) == LedgeClass::High);
    CHECK_FALSE(classifyLedge(2.3f, climb).has_value());
    CHECK(climbSeconds(LedgeClass::Mid, climb) == doctest::Approx(1.0f));

    const ClimbPath path{Vec3(0.0f), Vec3(0.0f, 1.5f, 1.0f), 1.0f, LedgeClass::Mid};
    CHECK(near(path.at(0.0f), Vec3(0.0f)));
    CHECK(near(path.at(0.7f), Vec3(0.0f, 1.5f, 0.0f))); // straight up first
    CHECK(near(path.at(1.0f), Vec3(0.0f, 1.5f, 1.0f)));
    CHECK(near(path.at(5.0f), Vec3(0.0f, 1.5f, 1.0f)));
    for (f32 t = 0.0f; t < 0.7f; t += 0.05f)
    {
        CHECK(path.at(t).z == doctest::Approx(0.0f)); // never into the wall on the way up
    }

    PlayerMovement m;
    const MovementSettings s;
    MoveInput in;
    in.forward = 1.0f;
    m.step(in, 0.1f, s);
    CHECK_FALSE(m.running(s));
    for (int i = 0; i < 10; ++i)
    {
        m.step(in, 0.1f, s);
    }
    CHECK(m.running(s));
    in.walk = true;
    for (int i = 0; i < 10; ++i)
    {
        m.step(in, 0.1f, s);
    }
    CHECK_FALSE(m.running(s));
    m.stop();
    CHECK(near(m.velocity(), Vec3(0.0f)));
}

TEST_CASE("movement.toml: jump, climb and fall")
{
    auto parsed = MovementSettings::parse(R"(
[jump]
stand_height = 0.8
run_height = 1.2
cooldown = 0
[climb]
low_max = 0.9
reach = 0.5
[fall]
safe_height = 3
damage_per_meter = 12
)",
                                          "movement.toml");
    REQUIRE_MESSAGE(parsed, (parsed ? "" : parsed.error().message));
    const MovementSettings& s = parsed.value();
    CHECK(s.jump.standHeight == doctest::Approx(0.8f));
    CHECK(s.jump.runHeight == doctest::Approx(1.2f));
    CHECK(s.jump.cooldown == doctest::Approx(0.0f));
    CHECK(s.climb.lowMax == doctest::Approx(0.9f));
    CHECK(s.climb.midMax == doctest::Approx(1.6f));
    CHECK(s.climb.reach == doctest::Approx(0.5f));
    CHECK(s.fall.safeHeight == doctest::Approx(3.0f));
    CHECK(s.fall.damagePerMeter == doctest::Approx(12.0f));
    CHECK_FALSE(MovementSettings::parse("[climb]\nmid_max = 2.5\n", "m.toml")); // above high_max
    CHECK_FALSE(MovementSettings::parse("[jump]\nrun_height = 0\n", "m.toml"));
}
