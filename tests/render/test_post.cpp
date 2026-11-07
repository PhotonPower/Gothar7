#include <g7/render/Bloom.hpp>
#include <g7/render/Grading.hpp>
#include <g7/render/Lighting.hpp>
#include <g7/render/PassTimes.hpp>
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

TEST_CASE("Bloom prefilter: nothing below the knee, softly into it, linear above; the hue stays (M17)")
{
    using render::bloomPrefilter;
    CHECK(glm::length(bloomPrefilter(Vec3(0.4f), 1.0f, 0.5f)) == doctest::Approx(0.0f));
    const Vec3 soft = bloomPrefilter(Vec3(0.9f), 1.0f, 0.5f); // inside the knee (0.5 .. 1.5)
    CHECK(soft.x > 0.0f);
    CHECK(soft.x < 0.9f - 0.5f);
    const Vec3 bright = bloomPrefilter(Vec3(4.0f, 2.0f, 1.0f), 1.0f, 0.5f);
    CHECK(bright.x == doctest::Approx(3.0f)); // brightness 4 - threshold 1
    CHECK(bright.y / bright.x == doctest::Approx(0.5f));
    CHECK(glm::length(bloomPrefilter(Vec3(1.2f), 1.0f, 0.0f)) == doctest::Approx(glm::length(Vec3(0.2f))));
}

TEST_CASE("Pass times: the first sample is taken, then smoothed")
{
    CHECK(render::smoothPassTime(0.0f, 2.0f, true) == doctest::Approx(2.0f));
    CHECK(render::smoothPassTime(2.0f, 4.0f, false) == doctest::Approx(2.2f));
}

TEST_CASE(
    "Grading: neutral is the identity, white balance, saturation, contrast, lift/gamma/gain, vignette (M17)")
{
    const Grading neutral;
    const Vec3 c(0.3f, 0.6f, 0.1f);
    const Vec3 same = gradeLinear(c, neutral);
    CHECK(same.x == doctest::Approx(c.x));
    CHECK(same.y == doctest::Approx(c.y));
    CHECK(same.z == doctest::Approx(c.z));
    CHECK(gradeDisplay(Vec3(0.25f, 0.5f, 0.75f), neutral).y == doctest::Approx(0.5f));
    CHECK(vignetteFactor(Vec2(0.5f), 0.3f) == doctest::Approx(1.0f));
    CHECK(vignetteFactor(Vec2(0.0f), 0.3f) == doctest::Approx(0.7f).epsilon(0.02));

    Grading warm;
    warm.temperature = 1.0f;
    const Vec3 w = gradeLinear(Vec3(0.5f), warm);
    CHECK(w.x > w.z); // red over blue
    CHECK(glm::dot(w, Vec3(0.2126f, 0.7152f, 0.0722f)) ==
          doctest::Approx(0.5f).epsilon(0.01)); // brightness kept

    Grading grey;
    grey.saturation = 0.0f;
    const Vec3 g = gradeLinear(c, grey);
    CHECK(g.x == doctest::Approx(g.y));
    CHECK(g.y == doctest::Approx(g.z));

    Grading hard;
    hard.contrast = 1.5f;
    CHECK(gradeLinear(Vec3(0.18f), hard).x == doctest::Approx(0.18f)); // middle grey stays
    CHECK(gradeLinear(Vec3(0.05f), hard).x < 0.05f);                   // darker below it
    CHECK(gradeLinear(Vec3(0.5f), hard).x > 0.5f);                     // brighter above

    Grading lifted;
    lifted.lift = Vec3(0.1f);
    CHECK(gradeDisplay(Vec3(0.0f), lifted).x == doctest::Approx(0.1f));
    Grading brighter;
    brighter.gamma = Vec3(2.0f);
    CHECK(gradeDisplay(Vec3(0.25f), brighter).x == doctest::Approx(0.5f));

    // Mixing and overrides (zones).
    const Grading half = mixGrading(neutral, warm, 0.5f);
    CHECK(half.temperature == doctest::Approx(0.5f));
    GradingOverride over;
    over.saturation = 0.7f;
    const Grading overridden = applyOverride(warm, over);
    CHECK(overridden.saturation == doctest::Approx(0.7f));
    CHECK(overridden.temperature == doctest::Approx(1.0f)); // the rest stays
}
