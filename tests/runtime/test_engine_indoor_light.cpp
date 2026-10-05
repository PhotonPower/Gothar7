// Rooms (label "headless"): zones of type indoor in the world file reach the lighting - the rooms nearest to
// the camera (world.md "Zonen", render.md); the ambient factor is environment.toml [indoor] (test_day_cycle).

#include <g7/runtime/Engine.hpp>

#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>

using namespace g7;

TEST_CASE("Engine lighting: the rooms of the world file, nearest first, with the indoor ambient")
{
    const std::filesystem::path world = std::filesystem::temp_directory_path() / "g7_indoor_light.g7world";
    {
        std::ofstream out(world, std::ios::binary);
        out << R"({"version":1,"name":"rooms","nextVobId":3,"vobs":[)"
            << R"({"id":1,"type":"mesh","name":"FLOOR","pos":[0.0,-0.75,0.0],"rot":[0.0,0.0,0.0,1.0],)"
            << R"("scale":[200.0,1.0,200.0],"mesh":"mobs/table.glb"},)"
            << R"({"id":2,"type":"start","name":"START","pos":[0.0,0.1,0.0],"rot":[0.0,0.0,0.0,1.0]}],)"
            << R"("zones":[)";
        // 40 rooms in a row along +X, every 5 m: only the 32 nearest go to the renderer.
        for (int i = 0; i < 40; ++i)
        {
            out << (i == 0 ? "" : ",") << R"({"type":"indoor","value":"ROOM_)" << i
                << R"(","box":{"center":[)" << i * 5
                << R"(,1.2,10.0],"halfExtents":[2.0,1.2,2.0],"yaw":0.0}})";
        }
        out << R"(,{"type":"music","value":"CAMP"}]})";
    }
    EngineConfig config;
    config.appName = "rooms";
    config.headless = true;
    config.world = world;
    config.start = "START";
    config.fixedFrameSeconds = 1.0 / 60.0;
    Engine engine(std::move(config));
    REQUIRE(engine.init().ok());
    const std::vector<render::IndoorVolume> near = engine.nearestIndoorVolumes(Vec3(0.0f));
    REQUIRE(near.size() == render::kMaxIndoorVolumes);
    f32 farthest = 0.0f;
    for (const render::IndoorVolume& v : near)
    {
        farthest = std::max(farthest, v.center.x);
    }
    CHECK(farthest < 160.0f); // the far rooms (x 160 .. 195) are left out
    CHECK(near[0].halfExtents == Vec3(2.0f, 1.2f, 2.0f));
    CHECK(engine.nearestIndoorVolumes(Vec3(195.0f, 0.0f, 10.0f))[0].center.x == doctest::Approx(195.0f));
    std::filesystem::remove(world);
}
