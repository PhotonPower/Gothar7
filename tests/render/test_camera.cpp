#include <g7/render/Camera.hpp>

#include <doctest/doctest.h>

using namespace g7;
using namespace g7::render;

namespace
{
Vec3 toNdc(const Camera& camera, const Vec3& world)
{
    const Vec4 clip = camera.viewProjection() * Vec4(world, 1.0f);
    return Vec3(clip) / clip.w;
}
} // namespace

TEST_CASE("Camera: a point straight ahead lands in the centre")
{
    Camera camera;
    camera.transform.position = Vec3(10, 2, 0);
    camera.transform.rotation = quatFromEuler(0.0f, toRadians(-90.0f), 0.0f); // looks along +X
    const Vec3 ndc = toNdc(camera, Vec3(30, 2, 0));
    CHECK(ndc.x == doctest::Approx(0.0f).epsilon(1e-5));
    CHECK(ndc.y == doctest::Approx(0.0f).epsilon(1e-5));
    CHECK(ndc.z > 0.0f);
    CHECK(ndc.z < 1.0f);

    // +Z (camera-right after the turn... world +Z is to the right when looking along +X).
    CHECK(toNdc(camera, Vec3(30, 2, 5)).x > 0.0f);
    CHECK(toNdc(camera, Vec3(30, 7, 0)).y > 0.0f); // above = up on screen

    CHECK(camera.frustum().contains(Vec3(30, 2, 0)));
    CHECK_FALSE(camera.frustum().contains(Vec3(-30, 2, 0)));
    CHECK_FALSE(camera.frustum().contains(Vec3(10 + 1600.0f, 2, 0))); // beyond far plane
}

TEST_CASE("Camera: scale does not affect the view")
{
    Camera camera;
    camera.transform.scale = Vec3(3.0f);
    const Vec3 ndc = toNdc(camera, Vec3(0, 1, -10));
    Camera unscaled;
    CHECK(nearlyEqual(ndc, toNdc(unscaled, Vec3(0, 1, -10))));
}

TEST_CASE("FreeFlyCamera: moves along the view, scaled by time")
{
    Camera camera;
    FreeFlyCamera fly;
    fly.attach(camera);
    fly.update(camera, {Vec3(0, 0, 1), 0.0f, Vec2(0.0f), false}, 0.5);
    CHECK(nearlyEqual(camera.transform.position, Vec3(0, 0, -5))); // 10 m/s * 0.5 s along -Z

    fly.update(camera, {Vec3(1, 0, 0), 0.0f, Vec2(0.0f), true}, 0.1);
    CHECK(nearlyEqual(camera.transform.position, Vec3(5, 0, -5))); // fast: 50 m/s * 0.1 s to the right

    // Diagonal input is normalised.
    Camera other;
    FreeFlyCamera fly2;
    fly2.update(other, {Vec3(1, 0, 1), 0.0f, Vec2(0.0f), false}, 1.0);
    CHECK(glm::length(other.transform.position) == doctest::Approx(10.0f));
}

TEST_CASE("FreeFlyCamera: rising follows world up even when looking down")
{
    Camera camera;
    FreeFlyCamera fly;
    fly.update(camera, {Vec3(0.0f), 0.0f, Vec2(0.0f, 300.0f), false}, 0.0); // look down 30°
    CHECK(camera.transform.forward().y < -0.4f);
    fly.update(camera, {Vec3(0, 1, 0), 0.0f, Vec2(0.0f), false}, 1.0);
    CHECK(nearlyEqual(camera.transform.position, Vec3(0, 10, 0)));
}

TEST_CASE("FreeFlyCamera: mouse look, pitch limit, keyboard turn")
{
    Camera camera;
    FreeFlyCamera fly;
    fly.update(camera, {Vec3(0.0f), 0.0f, Vec2(900.0f, 0.0f), false}, 0.0); // 90° to the right
    CHECK(nearlyEqual(camera.transform.forward(), Vec3(1, 0, 0), 1e-4f));

    fly.update(camera, {Vec3(0.0f), 0.0f, Vec2(0.0f, -5000.0f), false}, 0.0); // way up
    CHECK(fly.pitch() == doctest::Approx(toRadians(89.0f)));
    CHECK(camera.transform.forward().y < 1.0f);
    CHECK(nearlyEqual(camera.transform.up().y > 0.0f ? 1.0f : 0.0f, 1.0f)); // never flips

    Camera turning;
    FreeFlyCamera fly3;
    fly3.update(turning, {Vec3(0.0f), 1.0f, Vec2(0.0f), false}, 1.0); // 90°/s left for 1 s
    CHECK(nearlyEqual(turning.transform.forward(), Vec3(-1, 0, 0), 1e-4f));
}

TEST_CASE("FreeFlyCamera: attach adopts the camera orientation")
{
    Camera camera;
    camera.transform.rotation = quatFromEuler(toRadians(-20.0f), toRadians(135.0f), 0.0f);
    FreeFlyCamera fly;
    fly.attach(camera);
    CHECK(fly.pitch() == doctest::Approx(toRadians(-20.0f)));
    CHECK(fly.yaw() == doctest::Approx(toRadians(135.0f)));
    const Vec3 before = camera.transform.forward();
    fly.update(camera, {}, 0.016);
    CHECK(nearlyEqual(camera.transform.forward(), before, 1e-4f)); // no jump on the first update
}
