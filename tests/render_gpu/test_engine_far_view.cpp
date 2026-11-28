// Far view (render.md "Fernsicht"): big objects beyond view_distance stay in the picture up to
// far_view_distance at their lowest level of detail; small ones and everything beyond do not; 0 turns it off.

#include "GlFixture.hpp"
#include "RunTempDir.hpp"

#include <g7/runtime/Engine.hpp>

#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>

using namespace g7;

namespace
{
/// Along -Z from the start: a house-sized block at 900 m, a barrel-sized one at 900 m, a big one at 2000 m.
std::filesystem::path writeFarWorld()
{
    const std::filesystem::path world = test::runTempDir() / "g7_far_view.g7world";
    std::ofstream out(world, std::ios::binary);
    out << R"({"version":1,"name":"far_view","nextVobId":5,"vobs":[)"
        << R"({"id":1,"type":"mesh","name":"HAUS","pos":[0.0,5.0,-900.0],"rot":[0.0,0.0,0.0,1.0],)"
        << R"("scale":[10.0,10.0,10.0],"mesh":"testworld/block.gltf"},)"
        << R"({"id":2,"type":"mesh","name":"FASS","pos":[6.0,0.5,-900.0],"rot":[0.0,0.0,0.0,1.0],)"
        << R"("scale":[1.0,1.0,1.0],"mesh":"testworld/block.gltf"},)"
        << R"({"id":3,"type":"mesh","name":"FERN","pos":[0.0,5.0,-2000.0],"rot":[0.0,0.0,0.0,1.0],)"
        << R"("scale":[10.0,10.0,10.0],"mesh":"testworld/block.gltf"},)"
        << R"({"id":4,"type":"start","name":"START","pos":[0.0,0.0,0.0],"rot":[0.0,0.0,0.0,1.0]}]})";
    return world;
}

u32 farObjects(f64 farViewDistance)
{
    EngineConfig config;
    config.appName = "far_view";
    config.window.size = {160, 90};
    config.window.vsync = false;
    config.world = writeFarWorld();
    config.start = "START";
    config.player = false;
    config.shaderDirectory = fs::fromUtf8(G7_SHADER_DIR);
    config.fixedFrameSeconds = 1.0 / 60.0;
    config.settings.set<f64>("render.far_view_distance", farViewDistance);
    config.settings.set<f64>("render.view_distance", 400.0);
    test::keepVideoAlive();
    Engine engine(config);
    REQUIRE(engine.init().ok());
    for (int i = 0; i < 4; ++i)
    {
        REQUIRE(engine.runFrame());
    }
    CHECK(engine.renderDevice()->debugErrorCount() == 0);
    return engine.farViewObjects();
}
} // namespace

TEST_CASE(
    "Engine far view: a big object at 900 m is drawn, a small one and one at 2 km are not; 0 turns it off")
{
    CHECK(farObjects(1500.0) == 1); // HAUS only (FASS too small, FERN beyond)
    CHECK(farObjects(0.0) == 0);
}
