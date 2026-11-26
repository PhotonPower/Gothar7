// Reflection probes (M17 windows F2, render.md "Spiegelnde Fenster"): placement along paths (spacing,
// merging, deterministic), the face cameras (same tables as common/glass.glsl), the schedule (slots near the
// camera, budget per frame, finished probes only for the shader, redraw after invalidate).

#include <g7/render/ReflectionProbes.hpp>

#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <initializer_list>
#include <set>
#include <utility>
#include <vector>

using namespace g7;
using namespace g7::render;

namespace
{
bool near(const Vec3& a, const Vec3& b)
{
    return glm::length(a - b) < 1e-4f;
}

std::vector<ReflectionProbe> probesAt(std::initializer_list<f32> xs)
{
    std::vector<ReflectionProbe> probes;
    for (const f32 x : xs)
    {
        probes.push_back(
            {Vec3(x, 2.5f, 0.0f), AABB{Vec3(x - 10.0f, 0.0f, -10.0f), Vec3(x + 10.0f, 10.0f, 10.0f)}});
    }
    return probes;
}
} // namespace

TEST_CASE("Probes: every 20 m along a path, 2.5 m above it, none nearer than 10 m to another (F2)")
{
    const std::vector<std::pair<Vec3, Vec3>> street{{Vec3(0.0f), Vec3(100.0f, 0.0f, 0.0f)}};
    const std::vector<Vec3> probes = placeReflectionProbes({}, {}, street);
    REQUIRE(probes.size() == 6);
    for (usize i = 0; i < probes.size(); ++i)
    {
        CHECK(near(probes[i], Vec3(20.0f * static_cast<f32>(i), 2.5f, 0.0f)));
    }
    // A square first; a parallel lane 4 m away and a short path add nothing new.
    const std::vector<Vec3> squares{Vec3(50.0f, 0.0f, 0.0f)};
    const std::vector<std::pair<Vec3, Vec3>> more{{Vec3(0.0f), Vec3(100.0f, 0.0f, 0.0f)},
                                                  {Vec3(0.0f, 0.0f, 4.0f), Vec3(100.0f, 0.0f, 4.0f)},
                                                  {Vec3(51.0f, 0.0f, 1.0f), Vec3(52.0f, 0.0f, 1.0f)}};
    const std::vector<Vec3> merged = placeReflectionProbes({}, squares, more);
    REQUIRE(!merged.empty());
    CHECK(near(merged[0], Vec3(50.0f, 2.5f, 0.0f)));
    for (usize i = 0; i < merged.size(); ++i)
    {
        for (usize j = i + 1; j < merged.size(); ++j)
        {
            CHECK(glm::length(merged[i] - merged[j]) >= 10.0f);
        }
    }
    CHECK(merged.size() ==
          7); // the square and the street's six (40 and 60 are just 10 m away); the lane adds none
    CHECK(placeReflectionProbes({}, squares, more) == merged); // deterministic
    CHECK(placeReflectionProbes({}, {}, {}).empty());
    // Own spacing and height.
    CHECK(placeReflectionProbes({}, {}, street, {.spacing = 50.0f, .height = 1.0f, .merge = 5.0f}).size() ==
          3);
}

TEST_CASE("Probes: welt's VOB_PROBE_* markers stand as they are; the paths keep their distance to them")
{
    const std::vector<Vec3> markers{Vec3(30.0f, 2.5f, 0.0f), Vec3(35.0f, 6.0f, 0.0f)};
    const std::vector<std::pair<Vec3, Vec3>> street{{Vec3(0.0f), Vec3(100.0f, 0.0f, 0.0f)}};
    const std::vector<Vec3> probes = placeReflectionProbes(markers, {}, street);
    REQUIRE(probes.size() >= 2);
    CHECK(near(probes[0], markers[0])); // both, unmerged and not raised
    CHECK(near(probes[1], markers[1]));
    for (usize i = 2; i < probes.size(); ++i)
    {
        CHECK(probes[i].y == doctest::Approx(2.5f));
        CHECK(glm::length(Vec2(probes[i].x, probes[i].z) - Vec2(30.0f, 0.0f)) >= 10.0f);
    }
    CHECK(probes.size() == 2 + 5); // 0, 20, 60, 80, 100: 40 is within 10 m of the markers
}

TEST_CASE("Probes: the face cameras look along the cube's axes, x = cross(forward, up) as in the shader")
{
    for (u32 f = 0; f < kProbeFaces; ++f)
    {
        const Camera camera = probeFaceCamera(Vec3(1.0f, 2.0f, 3.0f), f, 60.0f);
        CHECK(near(camera.transform.forward(), kProbeFaceForward[f]));
        CHECK(near(camera.transform.up(), kProbeFaceUp[f]));
        CHECK(near(camera.transform.right(), glm::cross(kProbeFaceForward[f], kProbeFaceUp[f])));
        CHECK(camera.aspect == 1.0f);
        CHECK(camera.fovY == doctest::Approx(glm::half_pi<f32>()));
        // A point ahead lands in the middle of the face's image, one up and right of it in the upper right.
        const Mat4 vp = camera.viewProjection();
        const Vec4 ahead = vp * Vec4(Vec3(1.0f, 2.0f, 3.0f) + 10.0f * kProbeFaceForward[f], 1.0f);
        CHECK(std::abs(ahead.x / ahead.w) < 1e-3f);
        CHECK(std::abs(ahead.y / ahead.w) < 1e-3f);
        const Vec3 corner = Vec3(1.0f, 2.0f, 3.0f) + 10.0f * kProbeFaceForward[f] +
                            5.0f * glm::cross(kProbeFaceForward[f], kProbeFaceUp[f]) + 5.0f * kProbeFaceUp[f];
        const Vec4 clip = vp * Vec4(corner, 1.0f);
        CHECK(clip.x / clip.w == doctest::Approx(0.5f).epsilon(0.01));
        CHECK(clip.y / clip.w == doctest::Approx(0.5f).epsilon(0.01));
    }
}

TEST_CASE(
    "Probes: slots for the nearest within the radius, a face a frame, only finished ones for the shader")
{
    ProbeSchedule schedule({.faceSize = 64, .slots = 2, .radius = 80.0f, .range = 60.0f});
    schedule.setProbes(probesAt({0.0f, 50.0f, 70.0f, 200.0f}));
    std::vector<ProbeFace> faces;
    schedule.update(Vec3(0.0f), 1, faces);
    REQUIRE(faces.size() == 1);
    CHECK(faces[0].probe == 0); // nearest first
    CHECK(faces[0].face == 0);
    CHECK(schedule.slotsUsed() == 2); // three in the radius, two slots
    CHECK(schedule.shaderProbes().empty());

    // Five faces of the nearest drawn: still not for the shader; the sixth finishes it.
    for (u32 f = 0; f < 5; ++f)
    {
        schedule.update(Vec3(0.0f), 1, faces);
        REQUIRE(faces.size() == 1);
        CHECK(faces[0].face == f);
        schedule.faceDone(faces[0]);
        schedule.update(Vec3(0.0f), 0, faces);
        CHECK(schedule.shaderProbes().empty());
    }
    schedule.update(Vec3(0.0f), 1, faces);
    REQUIRE(faces.size() == 1);
    const u32 firstSlot = faces[0].slot;
    schedule.faceDone(faces[0]);
    schedule.update(Vec3(0.0f), 100, faces);
    REQUIRE(schedule.shaderProbes().size() == 1);
    CHECK(schedule.shaderProbes()[0].center == Vec4(0.0f, 2.5f, 0.0f, static_cast<f32>(firstSlot)));
    CHECK(schedule.shaderProbes()[0].boxMax == Vec4(10.0f, 10.0f, 10.0f, 0.0f));
    CHECK(faces.size() == 6); // the budget allows it: the second probe's six faces
    CHECK(std::ranges::all_of(faces, [](const ProbeFace& f) { return f.probe == 1; }));
    CHECK(schedule.slotsReady() == 1);

    // Invalidated: all faces due again, the old images stay for the shader meanwhile.
    for (const ProbeFace& f : faces)
    {
        schedule.faceDone(f);
    }
    schedule.invalidate();
    schedule.update(Vec3(0.0f), 3, faces);
    CHECK(faces.size() == 3); // the budget holds
    CHECK(schedule.shaderProbes().size() == 2);

    // Elsewhere: the far probe takes a slot, the ones left behind lose theirs.
    schedule.update(Vec3(200.0f, 0.0f, 0.0f), 1, faces);
    CHECK(schedule.slotsUsed() == 1);
    REQUIRE(faces.size() == 1);
    CHECK(faces[0].probe == 3);
    CHECK(schedule.shaderProbes().empty());
    // A face finished for a slot given away does not count for its new probe.
    schedule.faceDone({0, faces[0].slot, 1, {}});
    schedule.update(Vec3(200.0f, 0.0f, 0.0f), 6, faces);
    CHECK(faces.size() == 6);
}

TEST_CASE("Probes: the faces of the slots lie side by side in the atlas, none overlapping")
{
    const ProbeSchedule schedule({.faceSize = 64, .slots = 96});
    std::set<std::pair<i32, i32>> corners;
    bool inside = true;
    for (u32 s = 0; s < 96; ++s)
    {
        for (u32 f = 0; f < kProbeFaces; ++f)
        {
            const PixelRect tile = schedule.tile(s, f);
            inside = inside && tile.width == 64 && tile.height == 64 && tile.x % 64 == 0 &&
                     tile.y % 64 == 0 &&
                     tile.x + 64 <= static_cast<i32>(schedule.slotsPerRow() * kProbeFaces * 64) &&
                     tile.y + 64 <= 12 * 64;
            corners.emplace(tile.x, tile.y);
        }
    }
    CHECK(inside);
    CHECK(corners.size() == 96 * kProbeFaces);
}
