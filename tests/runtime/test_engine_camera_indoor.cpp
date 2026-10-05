// The player camera's indoor profile (label "headless"): under a roof the camera moves closer
// ([camera.indoor] in data/movement.toml), blended in. The roof: a small world written here - welt's table (a
// collision box) scaled to a floor and, 2.6 m up, to a ceiling.

#include <g7/runtime/Engine.hpp>

#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>

using namespace g7;

namespace
{
EngineConfig worldConfig(const char* world, const char* start)
{
    EngineConfig config;
    config.appName = "indoor";
    config.headless = true;
    config.world = fs::fromUtf8(world);
    config.start = start;
    config.fixedFrameSeconds = 1.0 / 60.0;
    return config;
}

void runSeconds(Engine& engine, f32 seconds)
{
    for (int i = 0; i < static_cast<int>(seconds * 60.0f); ++i)
    {
        REQUIRE(engine.runFrame());
    }
}
} // namespace

TEST_CASE("Engine camera: closer under a roof (indoor profile), the far one outside")
{
    {
        Engine engine(worldConfig("testworld/camp.g7world", "START_LAGER"));
        REQUIRE(engine.init().ok());
        runSeconds(engine, 2.0f);
        CHECK(engine.playerIndoorBlend() < 0.05f); // the open camp
        CHECK(engine.playerCameraDistance() > 2.5f);
    }
    // A room: floor and ceiling; the hero starts under the ceiling.
    const std::filesystem::path world = std::filesystem::temp_directory_path() / "g7_indoor_room.g7world";
    {
        std::ofstream out(world, std::ios::binary);
        out << R"({"version":1,"name":"room","nextVobId":4,"vobs":[)"
            << R"({"id":1,"type":"mesh","name":"FLOOR","pos":[0.0,-0.75,0.0],"rot":[0.0,0.0,0.0,1.0],)"
            << R"("scale":[20.0,1.0,20.0],"mesh":"mobs/table.glb"},)"
            << R"({"id":2,"type":"mesh","name":"CEILING","pos":[0.0,2.6,0.0],"rot":[0.0,0.0,0.0,1.0],)"
            << R"("scale":[5.0,1.0,8.0],"mesh":"mobs/table.glb"},)"
            << R"({"id":3,"type":"start","name":"START_ROOM","pos":[0.0,0.1,0.0],"rot":[0.0,0.0,0.0,1.0]}]})";
    }
    EngineConfig config = worldConfig("", "START_ROOM");
    config.world = world;
    Engine engine(std::move(config));
    REQUIRE(engine.init().ok());
    runSeconds(engine, 0.1f);
    CHECK(engine.playerIndoorBlend() < 0.5f); // blended in, not at once
    runSeconds(engine, 3.0f);
    CHECK(engine.playerIndoorBlend() > 0.95f);
    CHECK(engine.playerCameraDistance() <= 1.95f);
    std::filesystem::remove(world);
}

TEST_CASE("Engine camera: under a jettied upper floor along a house it stays the outside camera")
{
    // Only a strip of roof 0.6 m wide right above the hero (like a jetty over the street): outside.
    const std::filesystem::path world = std::filesystem::temp_directory_path() / "g7_indoor_jetty.g7world";
    {
        std::ofstream out(world, std::ios::binary);
        out << R"({"version":1,"name":"jetty","nextVobId":4,"vobs":[)"
            << R"({"id":1,"type":"mesh","name":"FLOOR","pos":[0.0,-0.75,0.0],"rot":[0.0,0.0,0.0,1.0],)"
            << R"("scale":[20.0,1.0,20.0],"mesh":"mobs/table.glb"},)"
            << R"({"id":2,"type":"mesh","name":"JETTY","pos":[0.0,2.6,0.0],"rot":[0.0,0.0,0.0,1.0],)"
            << R"("scale":[5.0,1.0,0.75],"mesh":"mobs/table.glb"},)"
            << R"({"id":3,"type":"start","name":"START_STREET","pos":[0.0,0.1,0.0],"rot":[0.0,0.0,0.0,1.0]}]})";
    }
    EngineConfig config = worldConfig("", "START_STREET");
    config.world = world;
    Engine engine(std::move(config));
    REQUIRE(engine.init().ok());
    runSeconds(engine, 2.0f);
    CHECK(engine.playerIndoorBlend() < 0.05f);
    std::filesystem::remove(world);
}
