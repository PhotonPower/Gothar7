// Window glass (M17 windows F1, render.md "Spiegelnde Fenster"): the option's stages and the glass's Fresnel
// (render::glassFresnel is the formula of common/glass.glsl), the lighting block carries the stage and the
// sky.

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
