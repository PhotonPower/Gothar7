// Rain (M17 weather part R): quality names and streak counts, the rain map's projection (top surfaces near
// 0).

#include <g7/render/Rain.hpp>

#include <doctest/doctest.h>

using namespace g7;
using namespace g7::render;

TEST_CASE("Rain: quality names and the number of streaks")
{
    CHECK(parseRainQuality("low") == RainQuality::Low);
    CHECK(parseRainQuality("high") == RainQuality::High);
    CHECK_FALSE(parseRainQuality("max"));
    CHECK(rainStreaks(RainQuality::Low) < rainStreaks(RainQuality::Medium));
    CHECK(rainStreaks(RainQuality::Medium) < rainStreaks(RainQuality::High));
    CHECK(rainStreaks(RainQuality::High) == 24000u);
}

TEST_CASE("Rain map: straight down around its centre; higher surfaces get smaller depth")
{
    const Vec3 centre(100.0f, 10.0f, -50.0f);
    const Mat4 vp = rainMapViewProjection(centre);
    const auto project = [&](const Vec3& p)
    {
        const Vec4 c = vp * Vec4(p, 1.0f);
        return Vec3(c) / c.w;
    };
    const Vec3 middle = project(centre);
    CHECK(middle.x == doctest::Approx(0.0f).epsilon(1e-4));
    CHECK(middle.y == doctest::Approx(0.0f).epsilon(1e-4));
    CHECK(middle.z == doctest::Approx(0.5f)); // the eye is half the height above
    const Vec3 roof = project(centre + Vec3(0.0f, 5.0f, 0.0f));
    CHECK(roof.z < middle.z);
    CHECK(roof.x == doctest::Approx(0.0f).epsilon(1e-4)); // straight down: x, z do not depend on the height
    const Vec3 edge = project(centre + Vec3(kRainMapRadius, 0.0f, 0.0f));
    CHECK(std::abs(edge.x) == doctest::Approx(1.0f));
}
