#include <g7/core/Geometry.hpp>
#include <g7/core/Transform.hpp>

#include <doctest/doctest.h>

using namespace g7;

namespace
{
/// Camera at the origin looking along -Z, 90° vertical FOV, square, near 1, far 100.
Frustum standardFrustum()
{
    return Frustum::fromViewProjection(perspectiveReverseZ(toRadians(90.0f), 1.0f, 1.0f, 100.0f));
}

f32 depthOf(const Mat4& projection, const Vec3& viewPoint)
{
    const Vec4 clip = projection * Vec4(viewPoint, 1.0f);
    return clip.z / clip.w;
}
} // namespace

TEST_CASE("Plane distance")
{
    const Plane ground = Plane::fromPointNormal(Vec3(0, 2, 0), Vec3(0, 10, 0));
    CHECK(ground.distance(Vec3(5, 3, -1)) == doctest::Approx(1.0f));
    CHECK(ground.distance(Vec3(0, 0, 0)) == doctest::Approx(-2.0f));
    const Plane fromCoefficients = Plane::fromCoefficients(Vec4(0, 0, 2, 4));
    CHECK(fromCoefficients.normal == Vec3(0, 0, 1));
    CHECK(fromCoefficients.d == doctest::Approx(2.0f));
}

TEST_CASE("Reverse-Z projection maps near to 1 and far to 0")
{
    const Mat4 p = perspectiveReverseZ(toRadians(70.0f), 16.0f / 9.0f, 0.1f, 1500.0f);
    CHECK(depthOf(p, Vec3(0, 0, -0.1f)) == doctest::Approx(1.0f));
    CHECK(depthOf(p, Vec3(0, 0, -1500.0f)) == doctest::Approx(0.0f).epsilon(1e-6));
    // Monotonic: further away = smaller depth.
    CHECK(depthOf(p, Vec3(0, 0, -10.0f)) > depthOf(p, Vec3(0, 0, -11.0f)));
    // Precision far away: points 1 m apart at 1 km still get distinct float depths.
    CHECK(depthOf(p, Vec3(0, 0, -1000.0f)) != depthOf(p, Vec3(0, 0, -1001.0f)));
}

TEST_CASE("Frustum contains points in front only")
{
    const Frustum frustum = standardFrustum();
    CHECK(frustum.contains(Vec3(0, 0, -10)));
    CHECK(frustum.contains(Vec3(9, -9, -10)));        // inside the 90° cone
    CHECK_FALSE(frustum.contains(Vec3(11, 0, -10)));  // outside to the right
    CHECK_FALSE(frustum.contains(Vec3(0, 0, 10)));    // behind
    CHECK_FALSE(frustum.contains(Vec3(0, 0, -0.5f))); // before the near plane
    CHECK_FALSE(frustum.contains(Vec3(0, 0, -101)));  // beyond far
}

TEST_CASE("Frustum against spheres and boxes")
{
    const Frustum frustum = standardFrustum();
    CHECK(frustum.intersects(Sphere{Vec3(0, 0, -50), 1.0f}));
    CHECK(frustum.intersects(Sphere{Vec3(0, 0, -101.5f), 2.0f})); // straddles far
    CHECK_FALSE(frustum.intersects(Sphere{Vec3(0, 0, 5), 2.0f}));
    CHECK_FALSE(frustum.intersects(Sphere{Vec3(30, 0, -10), 2.0f}));

    CHECK(frustum.intersects(AABB{Vec3(-1, -1, -11), Vec3(1, 1, -9)}));
    CHECK(frustum.intersects(AABB{Vec3(9, -1, -11), Vec3(20, 1, -9)})); // partly visible
    CHECK_FALSE(frustum.intersects(AABB{Vec3(12, -1, -11), Vec3(20, 1, -9)}));
    CHECK_FALSE(frustum.intersects(AABB{Vec3(-1, -1, 1), Vec3(1, 1, 3)}));
}

TEST_CASE("Frustum follows the view matrix")
{
    // Camera at x = 50 turned right (yaw -90°): looks along +X.
    Transform camera;
    camera.position = Vec3(50, 0, 0);
    camera.rotation = quatFromEuler(0.0f, toRadians(-90.0f), 0.0f);
    const Mat4 view = camera.inverse().toMatrix();
    const Frustum frustum =
        Frustum::fromViewProjection(perspectiveReverseZ(toRadians(90.0f), 1.0f, 1.0f, 100.0f) * view);
    CHECK(frustum.contains(Vec3(60, 0, 0)));
    CHECK_FALSE(frustum.contains(Vec3(40, 0, 0)));
    CHECK_FALSE(frustum.contains(Vec3(50, 0, -10)));
}

TEST_CASE("AABB transform")
{
    const AABB box{Vec3(-1, -2, -3), Vec3(1, 2, 3)};
    CHECK(box.center() == Vec3(0.0f));
    CHECK(box.extents() == Vec3(1, 2, 3));

    Transform t;
    t.position = Vec3(10, 0, 0);
    t.rotation = quatFromEuler(0.0f, toRadians(90.0f), 0.0f); // x <-> z swap
    const AABB moved = box.transformed(t.toMatrix());
    CHECK(nearlyEqual(moved.center(), Vec3(10, 0, 0)));
    CHECK(nearlyEqual(moved.extents(), Vec3(3, 2, 1), 1e-4f));
}
