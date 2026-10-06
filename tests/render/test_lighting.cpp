#include <g7/render/Lighting.hpp>

#include <doctest/doctest.h>

#include <vector>

using namespace g7;
using namespace g7::render;

TEST_CASE("Lighting: attenuation falls smoothly to exactly zero at the radius")
{
    CHECK(pointLightAttenuation(0.0f, 10.0f) == doctest::Approx(1.0f));
    CHECK(pointLightAttenuation(10.0f, 10.0f) == 0.0f);
    CHECK(pointLightAttenuation(12.0f, 10.0f) == 0.0f);
    CHECK(pointLightAttenuation(1.0f, 0.0f) == 0.0f); // degenerate light
    f32 previous = 2.0f;
    for (f32 d = 0.0f; d <= 10.0f; d += 0.25f)
    {
        const f32 a = pointLightAttenuation(d, 10.0f);
        CHECK(a <= previous); // monotonic
        CHECK(a >= 0.0f);
        previous = a;
    }
    CHECK(pointLightAttenuation(9.99f, 10.0f) < 1e-3f); // no visible edge at the radius
}

TEST_CASE("Lighting: per-object selection takes reaching lights, nearest first, at most 8")
{
    LightList lights;
    const AABB box{Vec3(-1.0f), Vec3(1.0f)};
    lights.add({Vec3(5, 0, 0), 3.0f, Vec3(1), 1.0f});     // 0: 4 m from the box, radius 3 -> no
    lights.add({Vec3(3, 0, 0), 3.0f, Vec3(1), 1.0f});     // 1: 2 m -> yes
    lights.add({Vec3(0, 0, 0), 1.0f, Vec3(1), 1.0f});     // 2: inside -> distance 0
    lights.add({Vec3(0, 2, 0), 5.0f, Vec3(1), 0.0f});     // 3: intensity 0 -> skipped
    lights.add({Vec3(0, 0, -2.5f), 2.0f, Vec3(1), 1.0f}); // 4: 1.5 m -> yes

    std::vector<u32> selected;
    lights.selectFor(box, selected);
    CHECK(selected == std::vector<u32>{2, 4, 1});

    for (int i = 0; i < 12; ++i)
    {
        lights.add({Vec3(0.0f), 2.0f, Vec3(1), 1.0f}); // all at distance 0
    }
    lights.selectFor(box, selected);
    REQUIRE(selected.size() == LightList::kMaxPerObject);
    CHECK(selected[0] == 2); // ties keep insertion order
    CHECK(selected[1] == 5);

    lights.clear();
    lights.selectFor(box, selected);
    CHECK(selected.empty());
}

TEST_CASE("Lighting: frame limit and GPU packing")
{
    LightList lights;
    for (u32 i = 0; i < LightList::kMaxPerFrame + 10; ++i)
    {
        lights.add({Vec3(f32(i), 0, 0), 2.0f, Vec3(1, 0.5f, 0), 2.0f});
    }
    CHECK(lights.lights().size() == LightList::kMaxPerFrame);

    Environment environment;
    environment.sunDirection = Vec3(0, 10, 0);
    environment.sunColor = Vec3(1, 0.5f, 0.25f);
    environment.sunIntensity = 2.0f;
    const GpuLighting gpu = packLighting(environment, lights);
    CHECK(nearlyEqual(Vec3(gpu.sunDirection), Vec3(0, 1, 0))); // normalised
    CHECK(nearlyEqual(Vec3(gpu.sunColor), Vec3(2, 1, 0.5f)));
    CHECK(gpu.counts[0] == static_cast<i32>(LightList::kMaxPerFrame));
    CHECK(gpu.pointPositionRadius[3] == Vec4(3, 0, 0, 2));
    CHECK(nearlyEqual(Vec3(gpu.pointColor[3]), Vec3(2, 1, 0)));
}

TEST_CASE(
    "Lighting: rooms (indoor zones) - inside incl. inner surfaces, fading through the wall, turned boxes")
{
    // A room 4 x 3 x 6 m (half extents 2, 1.5, 3) at the origin, turned 90 degrees: local +X points to -Z.
    const IndoorVolume room{Vec3(0.0f, 1.5f, 0.0f), Vec3(2.0f, 1.5f, 3.0f), glm::radians(90.0f)};
    const IndoorVolume rooms[] = {room};
    CHECK(indoorAmount(Vec3(0.0f, 1.5f, 0.0f), rooms, 0.3f) == doctest::Approx(1.0f));
    // Local X runs along world Z: the inner wall face at local x = 2 is world z = -2, fully inside.
    CHECK(indoorAmount(Vec3(0.0f, 1.5f, -2.0f), rooms, 0.3f) == doctest::Approx(1.0f));
    CHECK(indoorAmount(Vec3(0.0f, 1.5f, -2.15f), rooms, 0.3f) == doctest::Approx(1.0f - 0.1f / 0.3f));
    CHECK(indoorAmount(Vec3(0.0f, 1.5f, -2.35f), rooms, 0.3f) == doctest::Approx(0.0f)); // the outer face
    // Local Z (half 3) runs along world X.
    CHECK(indoorAmount(Vec3(2.9f, 1.5f, 0.0f), rooms, 0.3f) == doctest::Approx(1.0f));
    CHECK(indoorAmount(Vec3(3.5f, 1.5f, 0.0f), rooms, 0.3f) == doctest::Approx(0.0f));
    // Floor and ceiling surfaces count; the ground under the floor slab not.
    CHECK(indoorAmount(Vec3(0.0f, 0.0f, 0.0f), rooms, 0.3f) == doctest::Approx(1.0f));
    CHECK(indoorAmount(Vec3(0.0f, -0.5f, 0.0f), rooms, 0.3f) == doctest::Approx(0.0f));
    CHECK(indoorAmount(Vec3(0.0f, 1.5f, 0.0f), {}, 0.3f) == doctest::Approx(0.0f));

    // Packed for the shader: centre + cos yaw, half extents + sin yaw; count, factor and edge.
    Environment environment;
    environment.indoor = {room};
    environment.indoorAmbient = 0.35f;
    const GpuLighting gpu = packLighting(environment, LightList{});
    CHECK(gpu.indoorParams.x == 1.0f);
    CHECK(gpu.indoorParams.y == doctest::Approx(0.35f));
    CHECK(gpu.indoorParams.z == doctest::Approx(0.3f));
    CHECK(gpu.indoorBoxes[0].w == doctest::Approx(0.0f).epsilon(1e-6));
    CHECK(gpu.indoorBoxes[1].w == doctest::Approx(1.0f));
    CHECK(Vec3(gpu.indoorBoxes[1]) == Vec3(2.0f, 1.5f, 3.0f));
}
