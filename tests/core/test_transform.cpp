#include <g7/core/Transform.hpp>

#include <doctest/doctest.h>

using namespace g7;

namespace
{
Transform sample(f32 uniformScale = 2.0f)
{
    Transform t;
    t.position = Vec3(1.0f, -2.0f, 3.5f);
    t.rotation = quatFromEuler(toRadians(20.0f), toRadians(-35.0f), toRadians(10.0f));
    t.scale = Vec3(uniformScale);
    return t;
}

bool nearlyEqual(const Transform& a, const Transform& b)
{
    return g7::nearlyEqual(a.position, b.position, 1e-4f) && g7::nearlyEqual(a.rotation, b.rotation, 1e-5f) &&
           g7::nearlyEqual(a.scale, b.scale, 1e-4f);
}
} // namespace

TEST_CASE("transform: identity defaults and axes")
{
    const Transform t;
    CHECK(t.toMatrix() == Mat4(1.0f));
    CHECK(g7::nearlyEqual(t.forward(), Vec3(0, 0, -1)));
    CHECK(g7::nearlyEqual(t.right(), Vec3(1, 0, 0)));
    CHECK(g7::nearlyEqual(t.up(), Vec3(0, 1, 0)));
}

TEST_CASE("transform: yaw +90 degrees turns forward to -X")
{
    Transform t;
    t.rotation = quatFromEuler(0.0f, toRadians(90.0f), 0.0f);
    CHECK(g7::nearlyEqual(t.forward(), Vec3(-1, 0, 0)));
    CHECK(g7::nearlyEqual(t.right(), Vec3(0, 0, -1)));
    CHECK(g7::nearlyEqual(t.up(), Vec3(0, 1, 0)));
}

TEST_CASE("transform: matrix and transformPoint agree")
{
    Transform t = sample();
    t.scale = Vec3(1.0f, 2.0f, 3.0f); // non-uniform is fine for a single transform
    const Vec3 p(0.5f, -1.0f, 2.0f);
    CHECK(g7::nearlyEqual(Vec3(t.toMatrix() * Vec4(p, 1.0f)), t.transformPoint(p), 1e-4f));
    CHECK(g7::nearlyEqual(t.transformDirection(Vec3(0, 0, -1)), t.forward()));
}

TEST_CASE("transform: fromMatrix round trip")
{
    Transform t = sample();
    t.scale = Vec3(1.5f, 0.5f, 2.0f);
    CHECK(nearlyEqual(Transform::fromMatrix(t.toMatrix()), t));

    SUBCASE("mirrored")
    {
        Transform mirrored;
        mirrored.scale = Vec3(-1.0f, 1.0f, 1.0f);
        const Transform back = Transform::fromMatrix(mirrored.toMatrix());
        CHECK(g7::nearlyEqual(back.transformPoint(Vec3(1, 2, 3)), Vec3(-1, 2, 3)));
    }
}

TEST_CASE("transform: inverse")
{
    const Transform t = sample();
    CHECK(nearlyEqual(t * t.inverse(), Transform{}));
    CHECK(nearlyEqual(t.inverse() * t, Transform{}));

    const Vec3 p(4.0f, 5.0f, -6.0f);
    CHECK(g7::nearlyEqual(t.inverse().transformPoint(t.transformPoint(p)), p, 1e-4f));
}

TEST_CASE("transform: hierarchy matches matrix product")
{
    const Transform parent = sample(2.0f);
    Transform child;
    child.position = Vec3(0.0f, 1.0f, -3.0f);
    child.rotation = quatFromEuler(0.0f, toRadians(45.0f), 0.0f);
    child.scale = Vec3(0.5f);

    const Transform world = parent * child;
    const Mat4 expected = parent.toMatrix() * child.toMatrix();
    const Mat4 actual = world.toMatrix();
    for (int c = 0; c < 4; ++c)
    {
        for (int r = 0; r < 4; ++r)
        {
            CHECK(g7::nearlyEqual(actual[c][r], expected[c][r], 1e-4f));
        }
    }
}

TEST_CASE("transform: interpolate")
{
    Transform a;
    Transform b;
    b.position = Vec3(10.0f, 0.0f, 0.0f);
    b.rotation = quatFromEuler(0.0f, toRadians(90.0f), 0.0f);
    b.scale = Vec3(3.0f);

    CHECK(nearlyEqual(interpolate(a, b, 0.0f), a));
    CHECK(nearlyEqual(interpolate(a, b, 1.0f), b));

    const Transform mid = interpolate(a, b, 0.5f);
    CHECK(g7::nearlyEqual(mid.position, Vec3(5, 0, 0)));
    CHECK(g7::nearlyEqual(mid.scale, Vec3(2.0f)));
    CHECK(g7::nearlyEqual(mid.rotation, quatFromEuler(0.0f, toRadians(45.0f), 0.0f)));

    SUBCASE("takes the shortest path even if quaternion signs differ")
    {
        Transform negated = b;
        negated.rotation = -b.rotation;
        CHECK(g7::nearlyEqual(interpolate(a, negated, 0.5f).rotation, mid.rotation));
    }
}
