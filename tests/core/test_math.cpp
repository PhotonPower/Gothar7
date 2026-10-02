#include <g7/core/Math.hpp>

#include <doctest/doctest.h>

#include <format>

using namespace g7;

TEST_CASE("math: default construction is zero / identity")
{
    const Vec3 v;
    CHECK(v == Vec3(0.0f));
    const Mat4 m;
    CHECK(m == Mat4(1.0f));
    const Quat q;
    CHECK(nearlyEqual(q, Quat(1.0f, 0.0f, 0.0f, 0.0f)));
}

TEST_CASE("math: angle conversion")
{
    CHECK(nearlyEqual(toRadians(180.0f), kPi));
    CHECK(nearlyEqual(toDegrees(kPi / 2.0f), 90.0f));
    static_assert(toRadians(0.0f) == 0.0f);
}

TEST_CASE("math: nearlyEqual")
{
    CHECK(nearlyEqual(1.0f, 1.0f + 1e-6f));
    CHECK_FALSE(nearlyEqual(1.0f, 1.01f));
    CHECK(nearlyEqual(Vec3(1, 2, 3), Vec3(1, 2, 3.000001f)));
    CHECK_FALSE(nearlyEqual(Vec3(1, 2, 3), Vec3(1, 2.1f, 3)));

    const Quat q = glm::angleAxis(0.7f, glm::normalize(Vec3(1, 2, 3)));
    CHECK(nearlyEqual(q, -q));
    CHECK_FALSE(nearlyEqual(q, Quat(1, 0, 0, 0)));
}

TEST_CASE("math: euler conventions")
{
    // Positive yaw turns left: -Z -> -X.
    CHECK(nearlyEqual(quatFromEuler(0.0f, toRadians(90.0f), 0.0f) * kWorldForward, Vec3(-1, 0, 0)));
    // Positive pitch looks up: -Z -> +Y.
    CHECK(nearlyEqual(quatFromEuler(toRadians(90.0f), 0.0f, 0.0f) * kWorldForward, Vec3(0, 1, 0)));
    // Yaw is applied after pitch: pitch up 90, then yaw 90 still points up.
    CHECK(
        nearlyEqual(quatFromEuler(toRadians(90.0f), toRadians(90.0f), 0.0f) * kWorldForward, Vec3(0, 1, 0)));
    // Roll does not change the view direction.
    CHECK(nearlyEqual(quatFromEuler(0.0f, 0.0f, toRadians(45.0f)) * kWorldForward, kWorldForward));
}

TEST_CASE("math: lookRotation")
{
    const Vec3 target = glm::normalize(Vec3(3, 1, -2));
    const Quat q = lookRotation(target);
    CHECK(nearlyEqual(q * kWorldForward, target));
    // Up stays in the vertical plane: right vector has no y component.
    CHECK(nearlyEqual((q * kWorldRight).y, 0.0f));

    CHECK(nearlyEqual(lookRotation(kWorldForward), Quat(1, 0, 0, 0)));

    // Degenerate: looking straight up still yields a valid rotation.
    const Quat upQ = lookRotation(kWorldUp);
    CHECK(nearlyEqual(upQ * kWorldForward, kWorldUp));
    CHECK(nearlyEqual(glm::length(upQ), 1.0f));

    // Zero direction falls back to identity.
    CHECK(nearlyEqual(lookRotation(Vec3(0.0f)), Quat(1, 0, 0, 0)));
}

TEST_CASE("math: std::format support")
{
    CHECK(std::format("{}", Vec3(1, 2, 3)) == "(1, 2, 3)");
    CHECK(std::format("{:.1f}", Vec2(0.26f, 1.0f)) == "(0.3, 1.0)");
    CHECK(std::format("{}", Quat(1, 0, 0, 0)) == "quat(1, 0, 0, 0)");
}
