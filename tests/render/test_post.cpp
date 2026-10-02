#include <g7/render/Lighting.hpp>
#include <g7/render/PostProcess.hpp>

#include <doctest/doctest.h>

using namespace g7;
using namespace g7::render;

TEST_CASE("Tonemapping curves")
{
    for (const Tonemapper t : {Tonemapper::Aces, Tonemapper::Reinhard, Tonemapper::None})
    {
        CHECK(tonemap(Vec3(0.0f), t) == Vec3(0.0f));
        CHECK(tonemap(Vec3(-1.0f), t) == Vec3(0.0f)); // negative input is clamped
        f32 previous = -1.0f;
        for (f32 x = 0.0f; x < 16.0f; x += 0.25f)
        {
            const f32 y = tonemap(Vec3(x), t).x;
            CHECK(y >= previous); // monotonic
            CHECK(y <= 1.0f);
            previous = y;
        }
    }
    // HDR highlights roll off instead of clipping...
    CHECK(tonemap(Vec3(4.0f), Tonemapper::Aces).x < 1.0f);
    CHECK(tonemap(Vec3(4.0f), Tonemapper::Aces).x > tonemap(Vec3(2.0f), Tonemapper::Aces).x);
    CHECK(tonemap(Vec3(4.0f), Tonemapper::Reinhard).x == doctest::Approx(0.8f));
    // ...while "none" clips.
    CHECK(tonemap(Vec3(4.0f), Tonemapper::None).x == 1.0f);
    CHECK(tonemap(Vec3(0.5f), Tonemapper::None).x == 0.5f);

    CHECK(tonemapperFromName("ACES", Tonemapper::None) == Tonemapper::Aces);
    CHECK(tonemapperFromName("reinhard", Tonemapper::None) == Tonemapper::Reinhard);
    CHECK(tonemapperFromName("none", Tonemapper::Aces) == Tonemapper::None);
    CHECK(tonemapperFromName("filmic?", Tonemapper::Reinhard) == Tonemapper::Reinhard);
}

TEST_CASE("Distance fog")
{
    const f32 density = fogDensityFor(0.9f, 300.0f, 30.0f);
    CHECK(density == doctest::Approx(0.00562f).epsilon(0.01));
    CHECK(fogFactor(0.0f, 30.0f, density) == 0.0f);
    CHECK(fogFactor(30.0f, 30.0f, density) == 0.0f);
    CHECK(fogFactor(300.0f, 30.0f, density) == doctest::Approx(0.9f));
    CHECK(fogFactor(2000.0f, 30.0f, density) > 0.999f);
    f32 previous = 0.0f;
    for (f32 d = 0.0f; d < 600.0f; d += 10.0f)
    {
        const f32 f = fogFactor(d, 30.0f, density);
        CHECK(f >= previous);
        previous = f;
    }
    CHECK(fogFactor(500.0f, 30.0f, 0.0f) == 0.0f);    // density 0 = off
    CHECK(fogDensityFor(0.9f, 20.0f, 30.0f) == 0.0f); // end before start: off
}
