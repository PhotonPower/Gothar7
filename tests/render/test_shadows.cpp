#include <g7/render/Camera.hpp>
#include <g7/render/Shadows.hpp>

#include <doctest/doctest.h>

#include <array>
#include <cmath>

using namespace g7;
using namespace g7::render;

namespace
{
Camera testCamera()
{
    Camera camera;
    camera.aspect = 16.0f / 9.0f;
    camera.transform.position = Vec3(3, 2, 7);
    camera.transform.rotation = quatFromEuler(toRadians(-10.0f), toRadians(30.0f), 0.0f);
    return camera;
}

const Vec3 kSun = glm::normalize(Vec3(0.6f, 0.25f, 0.4f));

std::array<Vec3, 8> sliceCorners(const Camera& camera, f32 n, f32 f)
{
    const f32 tanY = std::tan(camera.fovY * 0.5f);
    const f32 tanX = tanY * camera.aspect;
    const Transform& t = camera.transform;
    std::array<Vec3, 8> corners;
    usize i = 0;
    for (const f32 d : {n, f})
    {
        for (const f32 sx : {-1.0f, 1.0f})
        {
            for (const f32 sy : {-1.0f, 1.0f})
            {
                corners[i++] =
                    t.position + t.forward() * d + t.right() * (sx * d * tanX) + t.up() * (sy * d * tanY);
            }
        }
    }
    return corners;
}
} // namespace

TEST_CASE("Shadows: cascade splits")
{
    const auto splits = cascadeSplits(0.1f, 150.0f, 4, 0.75f);
    REQUIRE(splits.size() == 5);
    CHECK(splits.front() == doctest::Approx(0.1f));
    CHECK(splits.back() == doctest::Approx(150.0f));
    for (usize i = 1; i < splits.size(); ++i)
    {
        CHECK(splits[i] > splits[i - 1]);
    }
    const auto uniform = cascadeSplits(0.1f, 150.0f, 4, 0.0f);
    CHECK(uniform[1] == doctest::Approx(0.1f + 149.9f / 4.0f));
    const auto logarithmic = cascadeSplits(0.1f, 150.0f, 4, 1.0f);
    CHECK(logarithmic[1] == doctest::Approx(0.1f * std::pow(1500.0f, 0.25f)));
    // The blend lies between both.
    CHECK(splits[1] > logarithmic[1]);
    CHECK(splits[1] < uniform[1]);
}

TEST_CASE("Shadows: every cascade contains its frustum slice")
{
    const Camera camera = testCamera();
    ShadowSettings settings;
    const auto cascades = computeCascades(camera, kSun, settings);
    REQUIRE(cascades.size() == 4);
    for (const Cascade& cascade : cascades)
    {
        for (const Vec3& corner : sliceCorners(camera, cascade.splitNear, cascade.splitFar))
        {
            const Vec4 clip = cascade.viewProjection * Vec4(corner, 1.0f);
            const Vec3 ndc = Vec3(clip) / clip.w;
            CHECK(std::abs(ndc.x) <= 1.0f);
            CHECK(std::abs(ndc.y) <= 1.0f);
            CHECK(ndc.z >= 0.0f);
            CHECK(ndc.z <= 1.0f);
        }
    }
    // Further cascades cover more ground per texel; atlas tiles are distinct.
    CHECK(cascades[3].texelWorldSize > cascades[0].texelWorldSize);
    CHECK(cascades[1].atlasRect == Vec4(0.5f, 0.0f, 0.5f, 0.5f));
    CHECK(cascades[2].atlasRect == Vec4(0.0f, 0.5f, 0.5f, 0.5f));
}

TEST_CASE("Shadows: rotating the camera does not resize cascades")
{
    Camera camera = testCamera();
    const auto before = computeCascades(camera, kSun, ShadowSettings{});
    camera.transform.rotation = quatFromEuler(toRadians(25.0f), toRadians(-140.0f), 0.0f);
    const auto after = computeCascades(camera, kSun, ShadowSettings{});
    for (usize i = 0; i < before.size(); ++i)
    {
        CHECK(after[i].texelWorldSize == before[i].texelWorldSize);
    }
}

TEST_CASE("Shadows: texel snapping keeps the shadow grid fixed in the world")
{
    ShadowSettings settings;
    Camera camera = testCamera();
    const auto texelOf = [&](const Cascade& cascade, const Vec3& world)
    {
        const Vec4 clip = cascade.viewProjection * Vec4(world, 1.0f);
        return Vec2(clip) * (static_cast<f32>(settings.resolution) * 0.5f);
    };
    const Vec3 probe(10.3f, 0.0f, -4.7f);
    const auto a = computeCascades(camera, kSun, settings);
    camera.transform.position += Vec3(0.013f, 0.0f, 0.027f); // a fraction of a texel
    const auto b = computeCascades(camera, kSun, settings);
    camera.transform.position += Vec3(3.71f, 0.4f, -1.9f); // many texels
    const auto c = computeCascades(camera, kSun, settings);
    for (usize i = 0; i < a.size(); ++i)
    {
        const Vec2 ta = texelOf(a[i], probe);
        const Vec2 tb = texelOf(b[i], probe);
        const Vec2 tc = texelOf(c[i], probe);
        // Whole-texel moves only: the fractional texel position of a world point never changes.
        for (const Vec2& t : {tb, tc})
        {
            const Vec2 delta = t - ta;
            CHECK(std::abs(delta.x - std::round(delta.x)) < 1e-2f);
            CHECK(std::abs(delta.y - std::round(delta.y)) < 1e-2f);
        }
    }
}

TEST_CASE("Shadows: culling casters against a cascade's light volume")
{
    // Camera at the origin looking along -Z; sun low in the +Z direction (behind the camera).
    Camera camera;
    camera.aspect = 16.0f / 9.0f;
    const Vec3 toSun = glm::normalize(Vec3(0.0f, 0.3f, 1.0f));
    const auto cascades = computeCascades(camera, toSun, ShadowSettings{});
    const Frustum nearVolume = Frustum::fromViewProjection(cascades[0].viewProjection);
    const Frustum view = camera.frustum();

    const auto box = [](const Vec3& centre) { return AABB{centre - Vec3(1.0f), centre + Vec3(1.0f)}; };
    // In view: drawn and casting.
    CHECK(view.intersects(box(Vec3(0, 0, -5))));
    CHECK(nearVolume.intersects(box(Vec3(0, 0, -5))));
    // Behind the camera towards the sun: not drawn, but its shadow falls into view, so it casts.
    CHECK_FALSE(view.intersects(box(Vec3(0, 3, 30))));
    CHECK(nearVolume.intersects(box(Vec3(0, 3, 30))));
    // Far to the side: neither.
    CHECK_FALSE(view.intersects(box(Vec3(200, 0, -5))));
    CHECK_FALSE(nearVolume.intersects(box(Vec3(200, 0, -5))));
}
