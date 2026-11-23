// Telekinesis (M17 part T): picking the item along the aim, the flight (lift, float, arrive), walls, falling
// and landing, the mana per second.

#include <g7/gameplay/Telekinesis.hpp>

#include <doctest/doctest.h>

#include <vector>

using namespace g7;
using namespace g7::gameplay;
using Phase = TelekinesisFlight::Phase;

TEST_CASE("Telekinesis: the item nearest to the aim, within range, small enough, allowed and visible")
{
    const TelekinesisSettings s; // 15 m, 20 degrees, 1.2 m
    const Vec3 eye(0.0f, 1.6f, 0.0f);
    const Vec3 aim(0.0f, 0.0f, -1.0f);
    std::vector<TelekinesisCandidate> items{
        {1, Vec3(2.0f, 0.1f, -8.0f), 0.2f},  // about 18 degrees off
        {2, Vec3(0.3f, 0.1f, -10.0f), 0.2f}, // nearly straight ahead
        {3, Vec3(0.0f, 0.1f, -20.0f), 0.2f}, // too far
        {4, Vec3(0.0f, 0.1f, -5.0f), 2.0f},  // too big (a ladder)
        {5, Vec3(-6.0f, 0.1f, -3.0f), 0.2f}, // beside
    };
    CHECK(pickTelekinesisTarget(items, eye, aim, s) == std::optional<u64>(2));
    items[1].allowed = false; // the story keeps it
    CHECK(pickTelekinesisTarget(items, eye, aim, s) == std::optional<u64>(1));
    CHECK_FALSE(pickTelekinesisTarget(items, eye, aim, s, [](const TelekinesisCandidate&) { return false; }));
    CHECK_FALSE(pickTelekinesisTarget(items, eye, Vec3(0.0f, 0.0f, 1.0f), s)); // nothing behind
    TelekinesisSettings bigger = s;
    bigger.maxSize = 3.0f;
    CHECK(pickTelekinesisTarget(items, eye, aim, bigger) == std::optional<u64>(4));
}

TEST_CASE("Telekinesis: lifts, floats to the hand at its speed and arrives")
{
    const TelekinesisSettings s; // lift 0.5 m at 1 m/s, then 3 m/s
    TelekinesisFlight flight(Vec3(0.0f, 0.1f, -10.0f), s);
    const Vec3 hand(0.0f, 1.4f, -0.5f);
    for (int i = 0; i < 30; ++i)
    {
        flight.carry(1.0f / 60.0f, hand);
    }
    CHECK(flight.phase() == Phase::Float);
    CHECK(flight.position().y == doctest::Approx(0.6f).epsilon(0.01));
    CHECK(flight.position().z == doctest::Approx(-10.0f));
    f32 seconds = 0.5f;
    while (flight.phase() == Phase::Float && seconds < 10.0f)
    {
        flight.carry(1.0f / 60.0f, hand);
        seconds += 1.0f / 60.0f;
    }
    CHECK(flight.phase() == Phase::Arrived);
    // About 9.5 m at 3 m/s less the last 0.8 m.
    CHECK(seconds ==
          doctest::Approx(0.5f + (glm::length(hand - Vec3(0, 0.6f, -10.0f)) - 0.8f) / 3.0f).epsilon(0.03));
    CHECK(flight.spin() > 0.0f); // it turns slowly
    CHECK_FALSE(flight.outOfReach(hand));
    CHECK(TelekinesisFlight(Vec3(0.0f, 0.0f, -30.0f), s).outOfReach(hand)); // beyond 15 m x 1.5
}

TEST_CASE("Telekinesis: a wall stops it where it is; it floats there")
{
    const TelekinesisSettings s;
    TelekinesisFlight flight(Vec3(0.0f, 0.6f, -10.0f), TelekinesisSettings{.lift = 0.0f});
    // A wall at z = -6.
    const auto sweep = [](const Vec3& from, const Vec3& direction, f32 distance) -> std::optional<f32>
    {
        if (direction.z <= 0.0f || from.z > -6.0f + 1e-3f)
        {
            return std::nullopt;
        }
        const f32 toWall = (-6.0f - from.z) / direction.z;
        return toWall < distance ? std::optional<f32>(std::max(toWall, 0.0f)) : std::nullopt;
    };
    for (int i = 0; i < 5 * 60; ++i)
    {
        flight.carry(1.0f / 60.0f, Vec3(0.0f, 0.6f, 0.0f), sweep);
    }
    CHECK(flight.phase() == Phase::Float);
    CHECK(flight.position().z == doctest::Approx(-6.0f).epsilon(0.01));
    CHECK(flight.stuck()); // held at the wall for seconds: it should fall (owner decision 2026-11-08)
    (void)s;
}

TEST_CASE("Telekinesis: blocked about a second, it is stuck; moving again resets; 0 never")
{
    const auto wall = [](const Vec3& from, const Vec3& direction, f32 distance) -> std::optional<f32>
    {
        if (direction.z <= 0.0f || from.z > -6.0f + 1e-3f)
        {
            return std::nullopt;
        }
        const f32 toWall = (-6.0f - from.z) / direction.z;
        return toWall < distance ? std::optional<f32>(std::max(toWall, 0.0f)) : std::nullopt;
    };
    const Vec3 hand(0.0f, 0.6f, 0.0f);
    TelekinesisFlight flight(Vec3(0.0f, 0.6f, -6.5f), TelekinesisSettings{.lift = 0.0f});
    f32 blocked = 0.0f;
    for (int i = 0; i < 3 * 60 && !flight.stuck(); ++i)
    {
        const f32 before = flight.position().z;
        flight.carry(1.0f / 60.0f, hand, wall);
        blocked = flight.position().z - before < 1e-4f ? blocked + 1.0f / 60.0f : 0.0f;
    }
    CHECK(flight.stuck());
    CHECK(blocked == doctest::Approx(1.0f).epsilon(0.05)); // about a second at the wall
    // Free again (the hero stepped aside): not stuck any more.
    flight.carry(1.0f / 60.0f, hand);
    CHECK_FALSE(flight.stuck());

    TelekinesisFlight never(Vec3(0.0f, 0.6f, -6.5f), TelekinesisSettings{.lift = 0.0f, .blockedDrop = 0.0f});
    for (int i = 0; i < 5 * 60; ++i)
    {
        never.carry(1.0f / 60.0f, hand, wall);
    }
    CHECK_FALSE(never.stuck());
}

TEST_CASE("Telekinesis: let go it falls with gravity to the ground and lands; landing at once")
{
    TelekinesisFlight flight(Vec3(0.0f, 0.1f, -10.0f), TelekinesisSettings{});
    for (int i = 0; i < 90; ++i)
    {
        flight.carry(1.0f / 60.0f, Vec3(0.0f, 1.4f, 0.0f));
    }
    const f32 height = flight.position().y;
    flight.release(0.1f);
    CHECK(flight.phase() == Phase::Falling);
    f32 seconds = 0.0f;
    while (flight.fall(1.0f / 60.0f) == Phase::Falling)
    {
        seconds += 1.0f / 60.0f;
    }
    CHECK(flight.phase() == Phase::Landed);
    CHECK(flight.position().y == doctest::Approx(0.1f));
    CHECK(seconds == doctest::Approx(std::sqrt(2.0f * (height - 0.1f) / 9.81f)).epsilon(0.15));
    // Carrying does nothing any more.
    CHECK(flight.carry(0.1f, Vec3(0.0f)) == Phase::Landed);

    TelekinesisFlight other(Vec3(0.0f, 2.0f, 0.0f), TelekinesisSettings{});
    other.release(0.0f);
    other.land();
    CHECK(other.phase() == Phase::Landed);
    CHECK(other.position().y == 0.0f);
}

TEST_CASE("Telekinesis: mana per second with fractions carried over")
{
    f32 carry = 0.0f;
    i32 total = 0;
    for (int i = 0; i < 3 * 60; ++i)
    {
        total += telekinesisManaDue(carry, 2.0f, 1.0f / 60.0f);
    }
    CHECK(total == doctest::Approx(6).epsilon(0.2)); // 2 a second for 3 s
    CHECK(total >= 5);
    CHECK(telekinesisManaDue(carry, 0.0f, 1.0f) == 0);
}
