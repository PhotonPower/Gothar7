// Character controller (M5 part C): walking, steps, slopes, sliding, walls, falling, terrain holes.

#include <g7/physics/Character.hpp>
#include <g7/physics/Physics.hpp>

#include <doctest/doctest.h>

#include <cmath>
#include <vector>

using namespace g7;
using namespace g7::physics;

namespace
{
constexpr f32 kStep = 1.0f / 60.0f;

ShapePart box(const Vec3& min, const Vec3& max)
{
    ShapePart part;
    part.kind = ShapePart::Kind::Hull;
    for (int i = 0; i < 8; ++i)
    {
        part.points.push_back(Vec3(i & 1 ? max.x : min.x, i & 2 ? max.y : min.y, i & 4 ? max.z : min.z));
    }
    return part;
}

/// A ramp rising along +x from x0 at `degrees`, `length` long (horizontally), 10 m wide.
ShapePart ramp(f32 x0, f32 length, f32 degrees)
{
    const f32 h = length * std::tan(glm::radians(degrees));
    ShapePart part;
    part.kind = ShapePart::Kind::Hull;
    for (const f32 z : {-5.0f, 5.0f})
    {
        part.points.push_back(Vec3(x0, 0.0f, z));
        part.points.push_back(Vec3(x0 + length, 0.0f, z));
        part.points.push_back(Vec3(x0 + length, h, z));
    }
    return part;
}

struct Scene
{
    PhysicsWorld world;

    Scene()
    {
        auto created = PhysicsWorld::create({.maxBodies = 256, .threads = 1});
        REQUIRE(created.ok());
        world = std::move(created).value();
        add(box(Vec3(-20.0f, -1.0f, -20.0f), Vec3(40.0f, 0.0f, 20.0f))); // floor, top at y 0
    }

    void add(const ShapePart& part)
    {
        const ShapeId shape = world.createShape(std::vector<ShapePart>{part}).value();
        REQUIRE(world.addStatic(shape, Vec3(0.0f), Quat(1, 0, 0, 0), Vec3(1.0f), Layer::World, 1).ok());
    }

    CharacterController character(const Vec3& feet)
    {
        world.optimize();
        auto c = CharacterController::create(world, {}, feet);
        REQUIRE_MESSAGE(c.ok(), (c.ok() ? "" : c.error().message));
        return std::move(c).value();
    }
};

void run(CharacterController& c, const Vec3& velocity, f32 seconds)
{
    for (f32 t = 0.0f; t < seconds; t += kStep)
    {
        c.update(kStep, velocity);
    }
}
} // namespace

TEST_CASE("Character: walks on flat ground at the wanted speed and stands still without input")
{
    Scene s;
    CharacterController c = s.character(Vec3(0.0f, 0.02f, 0.0f));
    run(c, Vec3(0.0f), 0.2f);
    CHECK(c.state() == MoveState::Ground);
    run(c, Vec3(4.0f, 0.0f, 0.0f), 1.0f);
    CHECK(c.feet().x == doctest::Approx(4.0f).epsilon(0.05));
    CHECK(std::abs(c.feet().y) < 0.05f);
    CHECK(c.state() == MoveState::Ground);
    const Vec3 stopped = c.feet();
    run(c, Vec3(0.0f), 1.0f);
    CHECK(glm::length(c.feet() - stopped) < 0.01f);
    CHECK(c.groundNormal().y == doctest::Approx(1.0f).epsilon(0.01));
}

TEST_CASE("Character: the step height is the highest step it walks up")
{
    // Default step height 0.4 m (movement.toml), walking and running: a sharp limit (Character.cpp).
    for (const f32 speed : {2.0f, 4.0f})
    {
        for (const auto& [height, climbs] :
             {std::pair{0.1f, true}, std::pair{0.2f, true}, std::pair{0.3f, true}, std::pair{0.39f, true},
              std::pair{0.43f, false}, std::pair{0.6f, false}})
        {
            CAPTURE(speed);
            CAPTURE(height);
            Scene s;
            s.add(box(Vec3(2.0f, 0.0f, -5.0f), Vec3(10.0f, height, 5.0f)));
            CharacterController c = s.character(Vec3(0.0f, 0.02f, 0.0f));
            run(c, Vec3(speed, 0.0f, 0.0f), 6.0f / speed); // ends on the step (x 2..10)
            if (climbs)
            {
                CHECK(c.feet().x > 4.0f);
                CHECK(c.feet().y == doctest::Approx(height).epsilon(0.05));
            }
            else
            {
                CHECK(c.feet().x < 2.0f - 0.25f);
                CHECK(c.feet().y < 0.05f);
            }
        }
    }
}

TEST_CASE("Character: slopes up to the limit are walkable, steeper ones slide")
{
    SUBCASE("40 degrees: walks up")
    {
        Scene s;
        s.add(ramp(1.0f, 8.0f, 40.0f));
        CharacterController c = s.character(Vec3(0.0f, 0.02f, 0.0f));
        run(c, Vec3(3.0f, 0.0f, 0.0f), 2.0f);
        CHECK(c.feet().y > 2.0f);
        CHECK(c.state() == MoveState::Ground);
        // The flat bottom rests on its rim; the drawn feet are on the slope below the centre.
        const Vec3 f = c.visualFeet();
        const f32 surface = (f.x - 1.0f) * std::tan(glm::radians(40.0f));
        CHECK(std::abs(f.y - surface) < 0.03f);
        CHECK(c.feet().y > surface + 0.1f);
    }
    SUBCASE("60 degrees: cannot walk up, slides down when put on it")
    {
        Scene s;
        s.add(ramp(1.0f, 4.0f, 60.0f));
        CharacterController c = s.character(Vec3(0.0f, 0.02f, 0.0f));
        run(c, Vec3(3.0f, 0.0f, 0.0f), 2.0f);
        CHECK(c.feet().y < 0.6f);

        // Feet on the slope half way up (x 3: height 2 * tan 60 = 3.46).
        c.teleport(Vec3(3.0f, 2.0f * std::tan(glm::radians(60.0f)) + 0.05f, 0.0f));
        bool slid = false;
        for (int i = 0; i < 30; ++i)
        {
            c.update(kStep, Vec3(3.0f, 0.0f, 0.0f)); // pushing uphill does not help
            slid = slid || c.state() == MoveState::Slide;
        }
        CHECK(slid);
        run(c, Vec3(3.0f, 0.0f, 0.0f), 2.0f);
        CHECK(c.feet().y < 1.0f); // down at the foot of the slope
    }
}

TEST_CASE("Character: walls stop it, it falls with gravity and lands")
{
    Scene s;
    s.add(box(Vec3(2.0f, 0.0f, -5.0f), Vec3(3.0f, 4.0f, 5.0f)));
    CharacterController c = s.character(Vec3(0.0f, 0.02f, 0.0f));
    run(c, Vec3(3.0f, 0.0f, 0.0f), 2.0f);
    CHECK(c.feet().x < 2.0f - 0.25f);
    CHECK(c.feet().x > 1.5f);
    CHECK(std::abs(c.feet().y) < 0.05f);

    c.teleport(Vec3(-5.0f, 5.0f, 0.0f));
    run(c, Vec3(0.0f), 0.5f);
    CHECK(c.state() == MoveState::Air);
    CHECK(c.velocity().y < -3.0f);
    CHECK(c.feet().y < 5.0f - 0.9f); // about g t^2 / 2 = 1.2 m
    run(c, Vec3(0.0f), 2.0f);
    CHECK(c.state() == MoveState::Ground);
    CHECK(std::abs(c.feet().y) < 0.05f);
    // No steering in the air: a fall keeps the horizontal velocity it had.
    c.teleport(Vec3(-5.0f, 5.0f, 0.0f));
    run(c, Vec3(3.0f, 0.0f, 0.0f), 0.3f);
    CHECK(std::abs(c.feet().x + 5.0f) < 0.01f);
}

TEST_CASE("Character: falls through a terrain hole")
{
    auto created = PhysicsWorld::create({.maxBodies = 64, .threads = 1});
    REQUIRE(created.ok());
    PhysicsWorld world = std::move(created).value();
    const u32 n = 9; // 8 x 8 cells of 2 m from (-8, -8), flat at 0; the 4 middle cells are a hole
    std::vector<f32> heights(static_cast<usize>(n) * n, 0.0f);
    std::vector<u8> holes(static_cast<usize>(n - 1) * (n - 1), 255);
    for (usize r = 3; r <= 4; ++r)
    {
        for (usize col = 3; col <= 4; ++col)
        {
            holes[r * (n - 1) + col] = 0;
        }
    }
    REQUIRE(world.addHeightfield({n, n, 2.0f, Vec2(-8.0f), heights, holes}).ok());
    auto made = CharacterController::create(world, {}, Vec3(-6.0f, 0.05f, 0.0f));
    REQUIRE(made.ok());
    CharacterController c = std::move(made).value();
    run(c, Vec3(0.0f), 0.2f);
    CHECK(c.state() == MoveState::Ground);
    run(c, Vec3(3.0f, 0.0f, 0.0f), 2.5f); // reaches the hole around x 0
    run(c, Vec3(0.0f), 1.0f);
    CHECK(c.feet().y < -1.0f);

    CHECK_FALSE(CharacterController::create(world, {.radius = 0.5f, .height = 0.9f}, Vec3(0.0f)).ok());
}

TEST_CASE("Character: jumps to the asked height, keeps its run, reports the fall at landing")
{
    Scene s;
    CharacterController c = s.character(Vec3(0.0f, 0.02f, 0.0f));
    run(c, Vec3(0.0f), 0.2f);
    REQUIRE(c.state() == MoveState::Ground);
    const auto settled = c.takeLanding(); // created 2 cm above the floor
    CHECK((!settled.has_value() || *settled < 0.05f));
    const f32 speed = std::sqrt(2.0f * 9.81f * 0.9f);
    CHECK(c.jump(speed));
    f32 top = 0.0f;
    int frames = 0;
    for (; frames < 120; ++frames)
    {
        c.update(kStep, Vec3(0.0f));
        top = std::max(top, c.feet().y);
        if (frames > 2 && c.state() == MoveState::Ground)
        {
            break;
        }
        CHECK_FALSE(c.jump(speed)); // not again in the air
    }
    CHECK(top == doctest::Approx(0.9f).epsilon(0.05));
    CHECK(frames * kStep == doctest::Approx(2.0f * speed / 9.81f).epsilon(0.08)); // flight time
    const auto landing = c.takeLanding();
    REQUIRE(landing.has_value());
    CHECK(*landing == doctest::Approx(0.9f).epsilon(0.05));
    CHECK_FALSE(c.takeLanding().has_value()); // once

    // From a run: the horizontal speed carries the jump about 4 m * flight time.
    run(c, Vec3(4.0f, 0.0f, 0.0f), 0.5f);
    const f32 start = c.feet().x;
    CHECK(c.jump(speed));
    c.update(kStep, Vec3(4.0f, 0.0f, 0.0f));
    while (c.state() == MoveState::Air)
    {
        c.update(kStep, Vec3(0.0f)); // no steering in the air: letting go changes nothing
    }
    CHECK(c.feet().x - start == doctest::Approx(4.0f * 2.0f * speed / 9.81f).epsilon(0.1));
}

TEST_CASE("Character: falls are measured from the top, sliding is no fall")
{
    Scene s;
    CharacterController c = s.character(Vec3(0.0f, 6.0f, 0.0f));
    run(c, Vec3(0.0f), 2.0f);
    const auto fall = c.takeLanding();
    REQUIRE(fall.has_value());
    CHECK(*fall == doctest::Approx(6.0f).epsilon(0.02));

    Scene steep;
    steep.add(ramp(1.0f, 4.0f, 60.0f));
    CharacterController slider = steep.character(Vec3(0.0f, 0.02f, 0.0f));
    run(slider, Vec3(0.0f), 0.2f);
    (void)slider.takeLanding();
    slider.teleport(Vec3(4.0f, 3.0f * std::tan(glm::radians(60.0f)) + 0.02f, 0.0f)); // on the slope, 5.2 m up
    run(slider, Vec3(0.0f), 3.0f);
    CHECK(slider.feet().y < 1.0f);
    const auto slid = slider.takeLanding();
    CHECK((!slid.has_value() || *slid < 0.5f)); // at most the first touch, not the 5 m slide
}

TEST_CASE("Character: ledges in reach with room on top are found, others not")
{
    const Vec3 east(1.0f, 0.0f, 0.0f);
    SUBCASE("1.5 m wall 0.3 m in front")
    {
        Scene s;
        s.add(box(Vec3(0.6f, 0.0f, -3.0f), Vec3(4.0f, 1.5f, 3.0f)));
        CharacterController c = s.character(Vec3(0.0f, 0.02f, 0.0f));
        run(c, Vec3(0.0f), 0.2f);
        const auto ledge = c.findLedge(east, 0.4f, 2.2f, 0.6f);
        REQUIRE(ledge.has_value());
        CHECK(ledge->height == doctest::Approx(1.5f).epsilon(0.02));
        CHECK(ledge->feet.y == doctest::Approx(1.5f).epsilon(0.02));
        CHECK(ledge->feet.x > 0.6f + 0.3f - 0.01f);                    // the whole cylinder on top
        CHECK_FALSE(c.findLedge(-east, 0.4f, 2.2f, 0.6f).has_value()); // nothing behind
        CHECK_FALSE(c.findLedge(east, 0.4f, 1.2f, 0.6f).has_value());  // above the allowed height
    }
    SUBCASE("too far, too high, no room")
    {
        Scene far;
        far.add(box(Vec3(1.5f, 0.0f, -3.0f), Vec3(4.0f, 1.5f, 3.0f)));
        CharacterController a = far.character(Vec3(0.0f, 0.02f, 0.0f));
        run(a, Vec3(0.0f), 0.2f);
        CHECK_FALSE(a.findLedge(east, 0.4f, 2.2f, 0.6f).has_value());

        Scene high;
        high.add(box(Vec3(0.6f, 0.0f, -3.0f), Vec3(4.0f, 2.6f, 3.0f)));
        CharacterController b = high.character(Vec3(0.0f, 0.02f, 0.0f));
        run(b, Vec3(0.0f), 0.2f);
        CHECK_FALSE(b.findLedge(east, 0.4f, 2.2f, 0.6f).has_value());

        Scene roof; // a 1 m ledge under a roof 2.2 m up: no room to stand on it
        roof.add(box(Vec3(0.6f, 0.0f, -3.0f), Vec3(4.0f, 1.0f, 3.0f)));
        roof.add(box(Vec3(0.6f, 2.2f, -3.0f), Vec3(4.0f, 3.0f, 3.0f)));
        CharacterController d = roof.character(Vec3(0.0f, 0.02f, 0.0f));
        run(d, Vec3(0.0f), 0.2f);
        CHECK_FALSE(d.findLedge(east, 0.4f, 2.2f, 0.6f).has_value());
    }
    SUBCASE("a walkable slope is no ledge")
    {
        Scene s;
        s.add(ramp(0.4f, 6.0f, 40.0f));
        CharacterController c = s.character(Vec3(0.0f, 0.02f, 0.0f));
        run(c, Vec3(0.0f), 0.2f);
        CHECK_FALSE(c.findLedge(east, 0.4f, 2.2f, 0.6f).has_value());
    }
}

TEST_CASE("Character: swimming moves without gravity, walls still stop it, the water catches a fall")
{
    Scene s;
    s.add(box(Vec3(3.0f, -5.0f, -5.0f), Vec3(4.0f, 5.0f, 5.0f))); // a wall at x 3
    CharacterController c = s.character(Vec3(0.0f, 3.0f, 0.0f));
    run(c, Vec3(0.0f), 0.3f); // falling ...
    CHECK(c.state() == MoveState::Air);
    for (int i = 0; i < 120; ++i) // ... into water: hovers, then swims against the wall
    {
        c.swim(kStep, Vec3(i < 60 ? 0.0f : 2.0f, 0.0f, 0.0f));
    }
    CHECK(c.feet().y > 2.0f); // no gravity
    CHECK(c.feet().x < 3.0f - 0.25f);
    CHECK(c.feet().x > 1.5f);
    run(c, Vec3(0.0f), 2.0f); // out of the water: falls and lands - counted from leaving the water
    const auto fall = c.takeLanding();
    REQUIRE(fall.has_value());
    CHECK(*fall < 3.0f);
}
