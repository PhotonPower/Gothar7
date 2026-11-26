// Window glass (M17 windows F1, render.md "Spiegelnde Fenster"): the option's stages and the glass's Fresnel
// (render::glassFresnel is the formula of common/glass.glsl), the lighting block carries the stage and the
// sky; F1b: the share of lit windows over the day and the night factor.

#include <g7/render/Glass.hpp>
#include <g7/render/Lighting.hpp>

#include <doctest/doctest.h>

#include <cmath>

using namespace g7;
using namespace g7::render;

TEST_CASE("Glass: the stages of [render] window_reflections")
{
    for (const WindowReflections quality :
         {WindowReflections::Off, WindowReflections::Low, WindowReflections::Medium, WindowReflections::High})
    {
        CHECK(parseWindowReflections(windowReflectionsName(quality)) == quality);
    }
    CHECK(parseWindowReflections("medium") == WindowReflections::Medium);
    CHECK_FALSE(parseWindowReflections("ultra").has_value());
    CHECK_FALSE(parseWindowReflections("").has_value());
}

TEST_CASE("Glass: discreet Fresnel - about 4 % head-on, at most half at grazing views (decision 1)")
{
    CHECK(glassFresnel(1.0f) == doctest::Approx(kGlassF0));
    CHECK(glassFresnel(0.0f) == doctest::Approx(kGlassMaxFresnel));
    CHECK(glassFresnel(-0.5f) == doctest::Approx(kGlassMaxFresnel)); // behind: clamped
    CHECK(glassFresnel(2.0f) == doctest::Approx(kGlassF0));
    // Rising monotonically towards grazing views.
    f32 previous = glassFresnel(1.0f);
    for (f32 c = 0.95f; c >= 0.0f; c -= 0.05f)
    {
        const f32 f = glassFresnel(c);
        CHECK(f >= previous);
        previous = f;
    }
    // Schlick below the cap: 60 degrees.
    const f32 m = 1.0f - std::cos(glm::radians(60.0f));
    CHECK(glassFresnel(std::cos(glm::radians(60.0f))) ==
          doctest::Approx(kGlassF0 + (1.0f - kGlassF0) * std::pow(m, 5.0f)));
}

TEST_CASE("Glass: the lighting block carries the zenith colour and the stage")
{
    Environment environment;
    CHECK(packLighting(environment, LightList{}).skyZenith.w == 0.0f); // off unless the engine says so
    environment.skyZenith = Vec3(0.2f, 0.4f, 0.8f);
    environment.windowReflections = WindowReflections::High;
    const GpuLighting gpu = packLighting(environment, LightList{});
    CHECK(gpu.skyZenith == Vec4(0.2f, 0.4f, 0.8f, 3.0f));
}

TEST_CASE("Glass: lit windows at night - many in the evening, few after midnight, some before sunrise (F1b)")
{
    const f32 evening = windowsAwake(19.5f);
    const f32 midnight = windowsAwake(0.0f);
    const f32 lateNight = windowsAwake(3.0f);
    const f32 dawn = windowsAwake(6.0f);
    CHECK(evening > 0.4f);
    CHECK(midnight < evening);
    CHECK(lateNight < midnight);
    CHECK(dawn > lateNight);
    CHECK(lateNight > 0.0f); // somebody is always awake
    // Wraps over the day; keys meet at midnight; every hour in 0..1.
    CHECK(windowsAwake(24.0f) == doctest::Approx(midnight));
    CHECK(windowsAwake(43.5f) == doctest::Approx(evening));
    CHECK(windowsAwake(-4.5f) == doctest::Approx(evening));
    CHECK(windowsAwake(std::nanf("")) == doctest::Approx(midnight));
    for (f32 h = 0.0f; h < 24.0f; h += 0.25f)
    {
        CHECK(windowsAwake(h) >= 0.0f);
        CHECK(windowsAwake(h) <= 1.0f);
    }
}

TEST_CASE("Glass: the glow shows by how dark it is - none with the sun up, full below the horizon (F1b)")
{
    CHECK(windowGlowNight(1.0f) == 0.0f);
    CHECK(windowGlowNight(0.12f) == 0.0f);
    CHECK(windowGlowNight(-0.08f) == 1.0f);
    CHECK(windowGlowNight(-1.0f) == 1.0f);
    const f32 dusk = windowGlowNight(0.02f);
    CHECK(dusk > 0.0f);
    CHECK(dusk < 1.0f);
    CHECK(windowGlowNight(-0.02f) > dusk);

    Environment environment;
    CHECK(packLighting(environment, LightList{}).glassGlow ==
          Vec4(0.0f)); // no glow unless the engine says so
    environment.windowsAwake = 0.5f;
    environment.windowGlowNight = 2.0f; // clamped
    environment.glassTime = 12.5f;
    CHECK(packLighting(environment, LightList{}).glassGlow == Vec4(0.5f, 1.0f, 12.5f, 0.0f));
}
