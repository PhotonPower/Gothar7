#include <g7/render/Lighting.hpp>

#include <doctest/doctest.h>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>
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

TEST_CASE("Lighting: the lights of a frame - effects first, then the nearest visible ones, windows only near")
{
    // The eye at the origin looking along -Z (90 degrees), far plane 1000 m.
    const Frustum view =
        Frustum::fromViewProjection(perspectiveReverseZ(glm::radians(90.0f), 1.0f, 0.1f, 1000.0f) *
                                    glm::lookAt(Vec3(0.0f), Vec3(0.0f, 0.0f, -1.0f), Vec3(0.0f, 1.0f, 0.0f)));
    const auto light = [](f32 z, f32 x = 0.0f, f32 radius = 5.0f)
    { return PointLight{Vec3(x, 1.0f, z), radius}; };
    std::vector<PointLight> world;
    for (int i = 0; i < 200; ++i)
    {
        world.push_back(light(-10.0f - static_cast<f32>(i), 0.0f)); // ahead, 10 .. 209 m
    }
    world.push_back(light(30.0f));              // behind: its reach does not come into view
    world.push_back(light(-20.0f, 0.0f, 0.0f)); // no reach
    std::vector<PointLight> windows{light(-15.0f, 0.5f, 3.0f), light(-40.0f, 0.5f, 3.0f)};
    const std::vector<PointLight> effects{light(5.0f, 0.0f, 4.0f)}; // the torch, behind the eye: still taken
    LightList out;
    LightBudget budget;       // 128 lights, windows within 25 m
    budget.distance = 500.0f; // all 200 ahead are candidates: the budget cuts
    selectFrameLights(effects, world, windows, Vec3(0.0f), view, budget, out);
    REQUIRE(out.lights().size() == 128);
    CHECK(out.lights()[0].position.z == doctest::Approx(5.0f));   // the effect first
    CHECK(out.lights()[1].position.z == doctest::Approx(-10.0f)); // then the nearest reach
    // The near window light is in, the far one (40 m) not; nothing behind or without reach.
    const auto has = [&](f32 z, f32 x)
    {
        return std::ranges::any_of(
            out.lights(), [&](const PointLight& l)
            { return std::abs(l.position.z - z) < 1e-3f && std::abs(l.position.x - x) < 1e-3f; });
    };
    CHECK(has(-15.0f, 0.5f));
    CHECK_FALSE(has(-40.0f, 0.5f));
    CHECK_FALSE(has(30.0f, 0.0f));
    CHECK(std::ranges::none_of(out.lights(), [](const PointLight& l) { return l.radius == 0.0f; }));
    // Nearest reach first.
    f32 previous = -1.0f;
    for (usize i = 1; i < out.lights().size(); ++i)
    {
        const f32 d = std::max(glm::length(out.lights()[i].position) - out.lights()[i].radius, 0.0f);
        CHECK(d >= previous - 1e-3f);
        previous = d;
    }
    // A smaller budget: still the effect first; beyond light_distance nothing.
    budget.maxLights = 4;
    selectFrameLights(effects, world, windows, Vec3(0.0f), view, budget, out);
    REQUIRE(out.lights().size() == 4);
    CHECK(out.lights()[0].position.z == doctest::Approx(5.0f));
    budget = LightBudget{};
    budget.distance = 20.0f;
    selectFrameLights({}, world, {}, Vec3(0.0f), view, budget, out);
    CHECK(std::ranges::all_of(out.lights(),
                              [](const PointLight& l) { return -l.position.z - l.radius <= 20.0f; }));
    CHECK(out.lights().size() == 15); // reach within 20 m: centres 10 .. 24 m (not the one without reach)
    // Effects beyond the budget are cut at it, never past the shader's 256.
    const std::vector<PointLight> many(300, light(-5.0f));
    budget = LightBudget{};
    budget.maxLights = 1000;
    selectFrameLights(many, world, windows, Vec3(0.0f), view, budget, out);
    CHECK(out.lights().size() == LightList::kMaxPerFrame);
}
