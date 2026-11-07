// Water (M17 W1): Fresnel, absorption, the wave normals, kinds of water from data/water.toml.

#include <g7/core/FileSystem.hpp>
#include <g7/render/Water.hpp>

#include <doctest/doctest.h>

#include <cstdlib>
#include <string>

using namespace g7;
using namespace g7::render;

TEST_CASE("Water: Fresnel and Beer-Lambert absorption")
{
    CHECK(waterFresnel(1.0f) == doctest::Approx(0.02f)); // looking straight down: little reflection
    CHECK(waterFresnel(0.0f) == doctest::Approx(1.0f));  // grazing: a mirror
    CHECK(waterFresnel(0.5f) < waterFresnel(0.2f));
    const Vec3 a(1.0f, 0.5f, 2.0f);
    CHECK(waterTransmittance(a, 0.0f).x == doctest::Approx(1.0f));
    CHECK(waterTransmittance(a, 2.0f).x == doctest::Approx(std::exp(-2.0f)));
    CHECK(waterTransmittance(a, 2.0f).y >
          waterTransmittance(a, 2.0f).z);                           // less absorbed channel stays brighter
    CHECK(waterTransmittance(a, -1.0f).x == doctest::Approx(1.0f)); // no negative depth
}

TEST_CASE("Water: quality names")
{
    CHECK(parseWaterQuality("low") == WaterQuality::Low);
    CHECK(parseWaterQuality("medium") == WaterQuality::Medium);
    CHECK(parseWaterQuality("high") == WaterQuality::High);
    CHECK_FALSE(parseWaterQuality("ultra"));
}

TEST_CASE("Water: the wave normals tile without a seam")
{
    constexpr u32 kSize = 64;
    const std::vector<u8> rgba = makeWaterNormals(kSize);
    REQUIRE(rgba.size() == kSize * kSize * 4);
    const auto at = [&](u32 x, u32 y, u32 c) { return static_cast<int>(rgba[(y * kSize + x) * 4 + c]); };
    int largestStep = 0;
    int seamStep = 0;
    for (u32 y = 0; y < kSize; ++y)
    {
        for (u32 c = 0; c < 2; ++c)
        {
            for (u32 x = 0; x + 1 < kSize; ++x)
            {
                largestStep = std::max(largestStep, std::abs(at(x + 1, y, c) - at(x, y, c)));
            }
            seamStep = std::max(seamStep, std::abs(at(0, y, c) - at(kSize - 1, y, c)));
        }
    }
    CHECK(largestStep > 0);
    CHECK(seamStep <= largestStep); // across the edge no bigger jump than inside
}

TEST_CASE("Water: kinds, default and errors")
{
    const auto parsed =
        WaterStyles::parse("version = 1\ndefault = \"pond\"\n[kind.pond]\nabsorption = [1.0, 1.0, "
                           "1.0]\n[kind.river]\nflow_speed = 0.5\nwave_scale = 2.0\n",
                           "water.toml");
    REQUIRE_MESSAGE(parsed.ok(), (parsed.ok() ? "" : parsed.error().message));
    const WaterStyles& styles = parsed.value();
    CHECK(styles.style("river").flowSpeed == doctest::Approx(0.5f));
    CHECK(styles.style("river").waveScale == doctest::Approx(2.0f));
    CHECK(&styles.style("") == &styles.style("pond"));      // no kind: the default (contract with welt)
    CHECK(&styles.style("swamp") == &styles.style("pond")); // unknown kind: the default
    const auto error = [](std::string_view text)
    {
        auto r = WaterStyles::parse(text, "water.toml");
        return r.ok() ? std::string() : r.error().message;
    };
    CHECK(error("version = 2\n") == "water.toml: version: must be 1");
    CHECK(error("version = 1\ndefault = \"lake\"\n") == "water.toml: default: names no [kind.lake]");
    CHECK(error("version = 1\n[kind.x]\nabsorption = [1.0]\n") ==
          "water.toml: kind.x: absorption must be [r, g, b]");
    CHECK(error("version = 1\n[kind.x]\nunderwater_visibility = -1.0\n") ==
          "water.toml: kind.x: underwater_visibility must not be negative");
    WaterStyle hue;
    hue.absorption = Vec3(1.0f, 0.5f, 2.0f);
    CHECK(underwaterAbsorption(hue).x == doctest::Approx(1.0f)); // no visibility: unchanged
    hue.underwaterVisibility = 10.0f;
    const Vec3 a = underwaterAbsorption(hue);
    CHECK(a.x / a.y == doctest::Approx(2.0f)); // the hue stays
    CHECK(error("version = 1\n[kind.x]\nwave_scale = 0.0\n") ==
          "water.toml: kind.x: wave_scale, shininess and shore_fade must be > 0");
}

TEST_CASE("Water: the shipped kinds (decision 5: Glems about 4 m visibility, pond about 3 m)")
{
    const auto loaded = fs::readText(fs::fromUtf8(G7_ASSET_SOURCE_DIR) / "data" / "water.toml");
    REQUIRE(loaded.ok());
    const auto parsed = WaterStyles::parse(loaded.value(), "water.toml");
    REQUIRE_MESSAGE(parsed.ok(), (parsed.ok() ? "" : parsed.error().message));
    const WaterStyle& river = parsed.value().style("river");
    const WaterStyle& pond = parsed.value().style("pond");
    CHECK(river.flowSpeed > 0.0f);
    CHECK(pond.flowSpeed == doctest::Approx(0.0f));
    CHECK(&parsed.value().style("") == &pond);
    // Visibility: at that depth at most a few percent of the light is left (green, the clearest channel).
    CHECK(waterTransmittance(river.absorption, 4.0f).y < 0.08f);
    CHECK(waterTransmittance(river.absorption, 2.0f).y > 0.15f);
    CHECK(waterTransmittance(pond.absorption, 3.0f).y < 0.08f);
    CHECK(river.refraction <= 0.02f);              // plan: at most 2 % of the screen width
    CHECK(pond.waveStrength < river.waveStrength); // the pond is calmer (decision 6)
    // Diving (owner, 2026-11-07): about 8 m in the pond, 10 m in the Glems; the surface keeps its absorption.
    CHECK(waterTransmittance(underwaterAbsorption(pond), 8.0f).y == doctest::Approx(0.02f).epsilon(0.05));
    CHECK(waterTransmittance(underwaterAbsorption(river), 10.0f).y == doctest::Approx(0.02f).epsilon(0.05));
    CHECK(waterTransmittance(underwaterAbsorption(pond), 4.0f).y > 0.1f);
}
