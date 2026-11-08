// Weather (M17 part E1, docs/design/wetter.md): states from weather.toml, a deterministic schedule, fades,
// locks, the mood mixed into the time of day, snapshots for save games.

#include <g7/core/FileSystem.hpp>
#include <g7/world/DayCycle.hpp>
#include <g7/world/Weather.hpp>

#include <doctest/doctest.h>

#include <ostream>
#include <set>
#include <string>

using namespace g7;
using namespace g7::world;

namespace
{
Weather shipped()
{
    const auto text = fs::readText(fs::fromUtf8(G7_ASSET_SOURCE_DIR) / "data" / "weather.toml");
    REQUIRE(text.ok());
    auto weather = Weather::parse(text.value(), "weather.toml");
    REQUIRE_MESSAGE(weather.ok(), (weather.ok() ? "" : weather.error().message));
    return std::move(weather).value();
}

constexpr const char* kTwo = R"(version = 1
start = "klar"
transition_minutes = 10.0
[state.klar]
duration = [1.0, 1.0]
next = { regen = 1.0 }
[state.regen]
rain = 0.8
sun = 0.5
shadow = 0.2
grading_saturation = 0.5
duration = [2.0, 2.0]
next = { klar = 1.0 }
)";

std::string errorOf(std::string_view toml)
{
    auto w = Weather::parse(toml, "weather.toml");
    return w.ok() ? std::string() : w.error().message;
}
} // namespace

TEST_CASE("Weather: errors name the entry")
{
    CHECK(errorOf("version = 2\n") == "weather.toml: version: must be 1");
    CHECK(errorOf("version = 1\n") == "weather.toml: state: needs at least one [state.<name>]");
    CHECK(errorOf("version = 1\n[state.a]\nrain = 2.0\n") ==
          "weather.toml: state.a: rain, clouds, wind and gusts must lie in 0..1");
    CHECK(errorOf("version = 1\n[state.a]\nnext = { b = 1.0 }\n") ==
          "weather.toml: state.a.next: unknown state 'b'");
    CHECK(errorOf("version = 1\n[state.a]\nduration = [3.0, 1.0]\n") ==
          "weather.toml: state.a: duration must be [min, max] game hours, 0 < min <= max");
    CHECK(errorOf("version = 1\nstart = \"x\"\n[state.a]\n") == "weather.toml: start: unknown state 'x'");
}

TEST_CASE("Weather: the schedule changes after the duration and fades over the transition")
{
    auto parsed = Weather::parse(kTwo, "w.toml");
    REQUIRE(parsed.ok());
    Weather w = std::move(parsed).value();
    w.start(0.0, 1);
    CHECK(w.current() == "klar");
    CHECK(w.nextChange() == doctest::Approx(60.0));
    CHECK(w.update(59.0).empty());
    const auto changes = w.update(60.0);
    REQUIRE(changes.size() == 1);
    CHECK(changes[0].first == "klar");
    CHECK(changes[0].second == "regen");
    CHECK(w.sample(60.0).rain == doctest::Approx(0.0f)); // the fade begins
    CHECK(w.sample(65.0).rain == doctest::Approx(0.4f)); // half way
    CHECK(w.sample(70.0).rain == doctest::Approx(0.8f)); // reached
    CHECK(w.sample(70.0).state == "regen");
    // A long jump (sleeping) goes through every change on the way: 2 h rain, 1 h clear, 2 h rain ...
    const auto many = w.update(60.0 + 6.0 * 60.0);
    CHECK(many.size() == 4);
}

TEST_CASE("Weather: held during dialogues, locked by the story, set by hand")
{
    auto parsed = Weather::parse(kTwo, "w.toml");
    REQUIRE(parsed.ok());
    Weather w = std::move(parsed).value();
    w.start(0.0, 1);
    CHECK(w.update(60.0, true).empty()); // a dialogue: waits
    CHECK(w.current() == "klar");
    CHECK(w.update(61.0).size() == 1); // after it
    w.lock(true);
    CHECK(w.update(1000.0).empty());
    CHECK(w.current() == "regen");
    CHECK(w.set("klar", 1000.0, 0.0)); // set() works while locked
    CHECK(w.sample(1000.0).rain == doctest::Approx(0.0f));
    CHECK_FALSE(w.set("schnee", 1000.0, 0.0));
}

TEST_CASE("Weather: the course is deterministic and comes back from a snapshot")
{
    Weather a = shipped();
    Weather b = shipped();
    a.start(8.0 * 60.0, 42);
    b.start(8.0 * 60.0, 42);
    std::vector<std::string> courseA;
    std::vector<std::string> courseB;
    for (f64 clock = 8.0 * 60.0; clock < 20.0 * 1440.0; clock += 30.0)
    {
        a.update(clock);
        b.update(clock);
        courseA.push_back(a.current());
        courseB.push_back(b.current());
    }
    CHECK(courseA == courseB);
    // Saved half way, restored into a fresh weather: the same course afterwards.
    Weather c = shipped();
    c.start(0.0, 7);
    Weather d = shipped();
    for (f64 clock = 0.0; clock < 5.0 * 1440.0; clock += 30.0)
    {
        c.update(clock);
    }
    REQUIRE(d.restore(c.snapshot()));
    for (f64 clock = 5.0 * 1440.0; clock < 10.0 * 1440.0; clock += 30.0)
    {
        c.update(clock);
        d.update(clock);
        REQUIRE(c.current() == d.current());
        CHECK(c.sample(clock).rain == doctest::Approx(d.sample(clock).rain));
    }
    WeatherSnapshot unknown = c.snapshot();
    unknown.state = "schnee";
    CHECK_FALSE(d.restore(unknown));
}

TEST_CASE("Weather: the shipped schedule - rain about every third day, storms about every tenth, morning fog")
{
    Weather w = shipped();
    constexpr int kDays = 2000;
    w.start(8.0 * 60.0, 2026);
    int rainDays = 0;
    int stormDays = 0;
    int fogDays = 0;
    std::set<std::string> stormHours;
    bool stormOutsideWindow = false;
    for (int day = 0; day < kDays; ++day)
    {
        bool rain = false;
        bool storm = false;
        bool fog = false;
        for (int minute = 0; minute < 1440; minute += 10)
        {
            const f64 clock = static_cast<f64>(day) * 1440.0 + minute;
            for (const auto& [from, to] : w.update(clock))
            {
                if (to == "gewitter" && (minute < 12 * 60 || minute >= 22 * 60 + 10))
                {
                    stormOutsideWindow = true;
                }
            }
            rain = rain || w.current() == "regen" || w.current() == "starkregen";
            storm = storm || w.current() == "gewitter";
            fog = fog || w.current() == "nebel";
        }
        rainDays += rain ? 1 : 0;
        stormDays += storm ? 1 : 0;
        fogDays += fog ? 1 : 0;
    }
    const f64 rainShare = static_cast<f64>(rainDays) / kDays;
    const f64 stormShare = static_cast<f64>(stormDays) / kDays;
    const f64 fogShare = static_cast<f64>(fogDays) / kDays;
    MESSAGE("rain days ", rainShare, ", storm days ", stormShare, ", fog days ", fogShare);
    CHECK(rainShare > 0.25);
    CHECK(rainShare < 0.42);
    CHECK(stormShare > 0.06);
    CHECK(stormShare < 0.15);
    CHECK(fogShare > 0.18);
    CHECK(fogShare < 0.33);
    CHECK_FALSE(stormOutsideWindow); // storms begin only between 12 and 22 o'clock
}

TEST_CASE("Weather: mixed into the time of day - darker, foggier, greyer, the state's grading")
{
    auto parsed = Weather::parse(kTwo, "w.toml");
    REQUIRE(parsed.ok());
    Weather w = std::move(parsed).value();
    w.start(0.0, 1);
    DaySample day;
    day.environment.sunIntensity = 2.0f;
    day.environment.fogDensity = 0.01f;
    day.grading.saturation = 1.0f;
    DaySample clear = day;
    w.apply(clear, 10.0);
    CHECK(clear.environment.sunIntensity == doctest::Approx(2.0f)); // klar changes nothing
    CHECK(clear.grading.saturation == doctest::Approx(1.0f));
    w.update(60.0);
    DaySample rainy = day;
    w.apply(rainy, 70.0);
    CHECK(rainy.environment.sunIntensity == doctest::Approx(1.0f));
    CHECK(rainy.grading.saturation == doctest::Approx(0.5f));
    CHECK(rainy.environment.shadowStrength == doctest::Approx(0.2f)); // overcast: nearly no shadows
    CHECK(clear.environment.shadowStrength == doctest::Approx(1.0f));
    DaySample halfway = day;
    w.apply(halfway, 65.0);
    CHECK(halfway.grading.saturation == doctest::Approx(0.75f));
    // Without the file: always clear.
    Weather none = Weather::fallback();
    none.start(0.0, 1);
    CHECK(none.update(100000.0).empty());
    CHECK(none.current() == "klar");
}

TEST_CASE("Weather: wetness rises in rain within minutes and dries over about an hour (M17 N)")
{
    auto parsed = Weather::parse(kTwo, "w.toml");
    REQUIRE(parsed.ok());
    Weather w = std::move(parsed).value();
    w.start(0.0, 1);
    w.update(30.0);
    CHECK(w.sample(30.0).wetness == doctest::Approx(0.0f)); // dry while clear
    w.update(60.0);                                         // rain (0.8) from 60
    w.update(65.0);
    const f32 early = w.sample(65.0).wetness;
    CHECK(early > 0.0f);
    w.update(75.0);
    CHECK(w.sample(75.0).wetness == doctest::Approx(1.0f)); // soaked after ~10 minutes
    w.update(180.0);                                        // clear again at 180
    w.update(210.0);
    const f32 drying = w.sample(210.0).wetness;
    CHECK(drying < 1.0f);
    CHECK(drying > 0.3f); // not dry after half an hour
    // Kept in a snapshot.
    Weather copy = std::move(Weather::parse(kTwo, "w.toml")).value();
    REQUIRE(copy.restore(w.snapshot()));
    CHECK(copy.sample(210.0).wetness == doctest::Approx(drying));
    // --weather for screenshots: as if it had rained a while.
    CHECK(copy.set("regen", 210.0, 0.0));
    copy.settle();
    CHECK(copy.sample(210.0).wetness == doctest::Approx(1.0f));
}

TEST_CASE("Lightning: thunder after distance / 343 m/s, flares of a flash, a storm's flashes (M17 G)")
{
    CHECK(thunderDelay(343.0f) == doctest::Approx(1.0f));
    CHECK(thunderDelay(3000.0f) == doctest::Approx(8.75f).epsilon(0.01)); // the far ones: up to 9 s
    CHECK(lightningBrightness(-0.1f, 2) == 0.0f);
    CHECK(lightningBrightness(0.0f, 2) == doctest::Approx(1.0f));
    CHECK(lightningBrightness(0.08f, 2) == 0.0f); // dark between two flares
    CHECK(lightningBrightness(0.12f, 2) > 0.3f);  // the second flare, weaker
    CHECK(lightningBrightness(0.12f, 2) < 1.0f);
    CHECK(lightningBrightness(0.5f, 3) == 0.0f);                   // over
    CHECK(lightningStrength(300.0f) > lightningStrength(2500.0f)); // near ones light more

    LightningStorm a(42);
    LightningStorm b(42);
    int flashes = 0;
    for (int i = 0; i < 60000; ++i) // 1000 game minutes in steps of 1/60
    {
        const auto fa = a.step(1.0 / 60.0, 0.15f, i);
        const auto fb = b.step(1.0 / 60.0, 0.15f, i);
        REQUIRE(fa.has_value() == fb.has_value()); // deterministic
        if (fa)
        {
            ++flashes;
            CHECK(fa->distance >= 300.0f);
            CHECK(fa->distance <= 3000.0f);
            CHECK(fa->flickers >= 1u);
        }
    }
    CHECK(flashes > 110); // about 150 at 0.15 per game minute
    CHECK(flashes < 190);
    LightningStorm calm(1);
    CHECK_FALSE(calm.step(100.0, 0.0f, 0.0));
}
