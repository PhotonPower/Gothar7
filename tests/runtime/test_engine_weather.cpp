// Weather in the engine (M17 E1, docs/design/wetter.md): the camp starts clear, Lua sets, reads and locks the
// weather, the schedule tells the scripts of each change, a save game keeps the weather and an old save
// without it leaves the weather as it is.

#include <g7/runtime/Engine.hpp>

#include <doctest/doctest.h>

#include <ostream>
#include <string>

using namespace g7;

namespace
{
EngineConfig campConfig()
{
    EngineConfig config;
    config.appName = "weather";
    config.headless = true;
    config.world = fs::fromUtf8("testworld/camp.g7world");
    config.start = "START_LAGER";
    config.startTime = "12:00";
    config.fixedFrameSeconds = 1.0 / 60.0;
    return config;
}

script::Value run(Engine& engine, std::string_view line)
{
    auto result = engine.runConsoleLine(line);
    REQUIRE_MESSAGE(result.ok(), std::string(line), ": ", (result.ok() ? "" : result.error().message));
    return result.value();
}
} // namespace

TEST_CASE("Engine weather: Lua sets, reads and locks it; the schedule reaches the scripts")
{
    Engine engine(campConfig());
    REQUIRE(engine.init().ok());
    REQUIRE(engine.runFrame());
    CHECK(engine.weather().current() == "klar");
    CHECK(engine.weather().states().size() >= 5); // data/weather.toml
    CHECK(run(engine, "set_weather('regen')").asBool());
    CHECK(run(engine, "weather().name").asString() == "regen");
    CHECK(run(engine, "weather().rain").asNumber() == doctest::Approx(0.5));
    CHECK(engine.weatherNow().rain == doctest::Approx(0.5f));
    CHECK_FALSE(run(engine, "set_weather('schnee')").asBool());
    CHECK_FALSE(engine.runConsoleLine("set_weather(3)").ok());
    // Locked: eleven hours (longer than any rain) pass without a change.
    run(engine, "lock_weather(true)");
    run(engine, "Story.changes = 0");
    run(engine,
        "on('weather_changed', function(old, new) Story.changes = Story.changes + 1; Story.last = new end)");
    run(engine, "time(23, 0)");
    REQUIRE(engine.runFrame());
    CHECK(run(engine, "Story.changes").asInteger() == 0);
    CHECK(engine.weather().current() == "regen");
    // Unlocked: the schedule changes it at its next decision and tells the scripts.
    run(engine, "lock_weather(false)");
    run(engine, "time(23, 30)");
    REQUIRE(engine.runFrame());
    CHECK(run(engine, "Story.changes").asInteger() > 0);
    CHECK(run(engine, "Story.last").asString() == engine.weather().current());
}

TEST_CASE("Engine weather: a save keeps it; an old save without weather leaves it as it is")
{
    Engine engine(campConfig());
    REQUIRE(engine.init().ok());
    REQUIRE(engine.runFrame());
    run(engine, "set_weather('gewitter')");
    run(engine, "lock_weather(true)");
    auto state = engine.captureGame();
    REQUIRE_MESSAGE(state.ok(), (state.ok() ? "" : state.error().message));
    REQUIRE(state.value().weather.has_value());
    CHECK(state.value().weather->state == "gewitter");
    CHECK(state.value().weather->locked);

    Engine fresh(campConfig());
    REQUIRE(fresh.init().ok());
    REQUIRE(fresh.runFrame());
    CHECK(fresh.weather().current() == "klar");
    REQUIRE(fresh.applyGame(state.value()).ok());
    CHECK(fresh.weather().current() == "gewitter");
    CHECK(fresh.weather().locked());

    save::GameState old = state.value();
    old.weather.reset(); // a save from before M17 E1
    Engine other(campConfig());
    REQUIRE(other.init().ok());
    REQUIRE(other.runFrame());
    REQUIRE(other.applyGame(old).ok());
    CHECK(other.weather().current() == "klar");
}

TEST_CASE("Engine weather: the rain layer of the ambience follows the rain")
{
    EngineConfig config = campConfig();
    config.weather = "regen";
    Engine engine(config);
    REQUIRE(engine.init().ok());
    REQUIRE(engine.runFrame());
    if (engine.audio() == nullptr)
    {
        return; // audio switched off on this machine
    }
    CHECK(engine.rainSound() == "amb_rain");
    run(engine, "set_weather('klar')");
    REQUIRE(engine.runFrame());
    CHECK(engine.rainSound().empty());
}

TEST_CASE("Engine weather: a thunderstorm flashes and thunders; clear weather does not (M17 G)")
{
    const auto flashes = [](const char* weather)
    {
        EngineConfig config = campConfig();
        config.weather = weather;
        config.settings.set<f64>("time.minute_seconds", 0.05); // 20 game minutes per real second
        Engine engine(config);
        REQUIRE(engine.init().ok());
        run(engine, "lock_weather(true)"); // the storm stays for the whole test
        for (int i = 0; i < 600; ++i)      // 10 s: 200 game minutes, about 30 flashes in a storm
        {
            REQUIRE(engine.runFrame());
        }
        if (engine.lightningCount() > 0 && engine.audio() != nullptr)
        {
            CHECK(engine.lastThunderDelay() ==
                  doctest::Approx(world::thunderDelay(engine.lastLightning()->distance)));
        }
        return engine.lightningCount();
    };
    CHECK(flashes("gewitter") > 5u);
    // A flash by script (story): now, lighting the world; the thunder after its distance.
    Engine engine(campConfig());
    REQUIRE(engine.init().ok());
    REQUIRE(engine.runFrame());
    run(engine, "lightning(686)");
    CHECK(engine.lightningCount() == 1u);
    CHECK(engine.lightningNow() > 0.5f);
    if (engine.audio() != nullptr)
    {
        CHECK(engine.lastThunderDelay() == doctest::Approx(2.0f));
    }
    CHECK_FALSE(engine.runConsoleLine("lightning(20)").ok());
    // A direction like yaw (degrees, 0 = -Z, positive to the left): the bolt shows there in the sky.
    run(engine, "lightning(900, 30)");
    REQUIRE(engine.lastLightning().has_value());
    CHECK(engine.lastLightning()->azimuth == doctest::Approx(glm::radians(-30.0f)));
    CHECK(flashes("regen") == 0u);
}
