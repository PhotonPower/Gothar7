// Game time and day cycle (M4): clock events, jumps, curves, sun and moon.

#include <g7/core/FileSystem.hpp>
#include <g7/world/DayCycle.hpp>
#include <g7/world/GameTime.hpp>

#include <doctest/doctest.h>

#include <ostream>
#include <string>
#include <vector>

using namespace g7;
using namespace g7::world;

TEST_CASE("Game time: minutes in order, a day wraps, scale")
{
    GameTime time(4.0); // 4 s per game minute
    time.setTime(0, 23, 58);
    std::vector<TimeEvent> events;
    time.setCallback([&](const TimeEvent& e) { events.push_back(e); });
    time.advance(2.0); // half a minute
    CHECK(events.empty());
    time.advance(10.0); // 23:58:30 + 2.5 min -> 00:01 of day 1
    REQUIRE(events.size() == 3);
    CHECK(events[0].kind == TimeEvent::Kind::Minute);
    CHECK(events[0].to == 23 * 60 + 59);
    CHECK(events[2].to == kMinutesPerDay + 1);
    CHECK(time.day() == 1);
    CHECK(time.hourOfDay() == doctest::Approx(1.0f / 60.0f).epsilon(0.001)); // 00:01
    time.setSecondsPerMinute(0.0);                                           // invalid: back to the default
    CHECK(time.secondsPerMinute() == 4.0);
}

TEST_CASE("Game time: jumps are one event, also for long simulation steps")
{
    GameTime time(4.0);
    time.setTime(2, 22, 0);
    std::vector<TimeEvent> events;
    time.setCallback([&](const TimeEvent& e) { events.push_back(e); });

    time.advanceTo(8); // sleeping until 8 o'clock: tomorrow morning
    REQUIRE(events.size() == 1);
    CHECK(events[0].kind == TimeEvent::Kind::Jumped);
    CHECK(events[0].from == 2 * kMinutesPerDay + 22 * 60);
    CHECK(events[0].to == 3 * kMinutesPerDay + 8 * 60);
    CHECK(time.day() == 3);

    events.clear();
    time.advanceTo(12, 30); // still ahead today
    CHECK(time.day() == 3);
    CHECK(time.hourOfDay() == doctest::Approx(12.5f));

    // A step of 2 game hours (e.g. a high time scale) is a jump, not 120 minute events.
    events.clear();
    time.advance(120.0 * 4.0);
    REQUIRE(events.size() == 1);
    CHECK(events[0].kind == TimeEvent::Kind::Jumped);
    // Exactly kMaxMinuteEvents still come one by one.
    events.clear();
    time.advance(static_cast<f64>(GameTime::kMaxMinuteEvents) * 4.0);
    CHECK(events.size() == GameTime::kMaxMinuteEvents);

    // The exact clock goes into the save game and comes back.
    const f64 clock = time.clock();
    GameTime loaded;
    loaded.restore(clock);
    CHECK(loaded.totalMinutes() == time.totalMinutes());
}

namespace
{
const char* const kCurves = R"(version = 1
[orbit]
sunrise = 6.0
sunset = 18.0
noon_elevation = 60.0
moon_elevation = 45.0
[[key]]
hour = 0.0
sun_intensity = 0.0
moon_intensity = 0.3
fog_color = [0.0, 0.0, 0.0]
stars = 1.0
[[key]]
hour = 12.0
sun_intensity = 2.0
fog_color = [1.0, 1.0, 1.0]
fog_density = 2.0
stars = 0.0
)";
} // namespace

TEST_CASE("Day cycle: sun east at sunrise, south at noon, moon opposite")
{
    const Orbit orbit;
    const Vec3 rise = orbit.sunDirection(6.0f);
    CHECK(rise.x == doctest::Approx(1.0f).epsilon(0.001)); // east
    CHECK(rise.y == doctest::Approx(0.0f).epsilon(0.001));
    const Vec3 noon = orbit.sunDirection(12.0f);
    CHECK(noon.x == doctest::Approx(0.0f).epsilon(0.001));
    CHECK(noon.y == doctest::Approx(std::sin(glm::radians(60.0f))).epsilon(0.001));
    CHECK(noon.z > 0.0f);                                                        // south
    CHECK(orbit.sunDirection(18.0f).x == doctest::Approx(-1.0f).epsilon(0.001)); // west
    CHECK(orbit.sunDirection(0.0f).y < -0.5f);                                   // below at midnight
    const Vec3 moon = orbit.moonDirection(0.0f);
    CHECK(moon.y == doctest::Approx(std::sin(glm::radians(45.0f))).epsilon(0.001));
}

TEST_CASE("Day cycle: keys interpolate cyclically; sun by day, moon by night")
{
    auto cycle = DayCycle::parse(kCurves, "environment.toml");
    REQUIRE_MESSAGE(cycle.ok(), (cycle.ok() ? "" : cycle.error().message));
    const DaySample noon = cycle.value().evaluate(12.0f, 0.01f);
    CHECK(noon.environment.sunIntensity == doctest::Approx(2.0f));
    CHECK(noon.environment.sunDirection.y > 0.5f);
    CHECK(noon.environment.fogColor == Vec3(1.0f)); // sRGB white is linear white
    CHECK(noon.environment.fogDensity == doctest::Approx(0.02f));
    CHECK(noon.sky.stars == 0.0f);

    const DaySample night = cycle.value().evaluate(0.0f, 0.01f);
    CHECK(night.environment.sunDirection.y > 0.5f); // the moon is the light now
    CHECK(night.environment.sunIntensity == doctest::Approx(0.3f));
    CHECK(night.sky.stars == 1.0f);
    CHECK(night.sky.moon == doctest::Approx(1.0f));

    // 18:00 lies between noon and midnight (going round): halfway, smoothly.
    const DaySample evening = cycle.value().evaluate(18.0f, 0.01f);
    CHECK(evening.sky.stars == doctest::Approx(0.5f));
    CHECK(cycle.value().evaluate(30.0f, 0.01f).sky.stars ==
          doctest::Approx(cycle.value().evaluate(6.0f, 0.01f).sky.stars));
    // Between 06:00 and 12:00 the stars fade out monotonically.
    CHECK(cycle.value().evaluate(7.0f, 0.01f).sky.stars > cycle.value().evaluate(9.0f, 0.01f).sky.stars);
}

TEST_CASE("Day cycle: errors name the key; the shipped curves load")
{
    const auto error = [](std::string_view toml)
    {
        auto cycle = DayCycle::parse(toml, "environment.toml");
        REQUIRE_FALSE(cycle.ok());
        return cycle.error().message;
    };
    CHECK(error("version = 2\n[[key]]\nhour = 0.0\n") == "environment.toml: version: must be 1");
    CHECK(error("version = 1\n") == "environment.toml: key: needs at least one [[key]]");
    CHECK(error("version = 1\n[[key]]\nhour = 25.0\n") ==
          "environment.toml: key[0].hour: needs an hour 0 <= hour < 24");
    CHECK(error("version = 1\n[[key]]\nhour = 1.0\nfog_color = [1.0, 0.5]\n") ==
          "environment.toml: key[0].fog_color: must be [r, g, b] (sRGB, 0..1)");
    CHECK(error("version = 1\n[[key]]\nhour = 1.0\n[[key]]\nhour = 1.0\n") ==
          "environment.toml: key: two keys at hour 1");
    CHECK(error("version = 1\n[orbit]\nsunrise = 19.0\n[[key]]\nhour = 1.0\n") ==
          "environment.toml: orbit: needs 0 <= sunrise < sunset <= 24");

    // The content file of the game (assets/source/data/environment.toml).
    std::string text;
    {
        const auto loaded = fs::readText(fs::fromUtf8(G7_ASSET_SOURCE_DIR) / "data" / "environment.toml");
        REQUIRE(loaded.ok());
        text = loaded.value();
    }
    auto shipped = DayCycle::parse(text, "environment.toml");
    REQUIRE_MESSAGE(shipped.ok(), (shipped.ok() ? "" : shipped.error().message));
    CHECK(shipped.value().keys().size() >= 6);
    CHECK(shipped.value().evaluate(0.0f, 0.01f).sky.stars > 0.5f);
    CHECK(shipped.value().evaluate(12.5f, 0.01f).environment.sunIntensity >
          shipped.value().evaluate(19.5f, 0.01f).environment.sunIntensity);
}
