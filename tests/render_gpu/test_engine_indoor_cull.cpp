// Furniture of rooms (render.md "Innenräume", label "gpu"): what lies wholly in an indoor zone is neither
// drawn nor casts a shadow while the camera is in no room and farther than [render] indoor_cull_distance from
// that room; near it or inside a room it is drawn.

#include "GlFixture.hpp"
#include "RunTempDir.hpp"

#include <g7/runtime/Engine.hpp>

#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>

using namespace g7;

namespace
{
struct Counts
{
    u32 main = 0;
    u32 shadows = 0;
};

/// A room 6 x 3 x 6 m at the origin with a table in it, a second table outside it (x 10), a second room
/// (x 40) without furniture; the camera at `camera` looking at the first room.
Counts culledAt(const Vec3& camera, f32 yawDegrees, f64 cullDistance = 12.0)
{
    const std::filesystem::path world = test::runTempDir() / "g7_indoor_cull.g7world";
    {
        std::ofstream out(world, std::ios::binary);
        out << R"({"version":1,"name":"cull","nextVobId":4,"vobs":[)"
            << R"({"id":1,"type":"mesh","name":"TABLE_IN","pos":[0.0,0.0,0.0],"rot":[0.0,0.0,0.0,1.0],"mesh":"mobs/table.glb"},)"
            << R"({"id":2,"type":"mesh","name":"TABLE_OUT","pos":[10.0,0.0,0.0],"rot":[0.0,0.0,0.0,1.0],"mesh":"mobs/table.glb"},)"
            << R"({"id":3,"type":"start","name":"START","pos":[0.0,0.1,-60.0],"rot":[0.0,0.0,0.0,1.0]}],)"
            << R"("zones":[)"
            << R"({"type":"indoor","value":"STUBE","box":{"center":[0.0,1.5,0.0],"halfExtents":[3.0,1.5,3.0],"yaw":0.0}},)"
            << R"({"type":"indoor","value":"LEER","box":{"center":[40.0,1.5,0.0],"halfExtents":[3.0,1.5,3.0],"yaw":0.0}}]})";
    }
    EngineConfig config;
    config.appName = "cull";
    config.window.size = {160, 90};
    config.window.vsync = false;
    config.world = world;
    config.start = "START";
    config.startTime = "12:00";
    config.shaderDirectory = fs::fromUtf8(G7_SHADER_DIR);
    config.fixedFrameSeconds = 1.0 / 60.0;
    config.settings.set<f64>("render.indoor_cull_distance", cullDistance);
    config.view.camera = camera;
    config.view.yawDegrees = yawDegrees;
    config.view.pitchDegrees = -15.0f;
    config.view.fly = true;
    test::keepVideoAlive();
    Engine engine(config);
    REQUIRE(engine.init().ok());
    for (int i = 0; i < 3; ++i)
    {
        REQUIRE(engine.runFrame());
    }
    CHECK(engine.renderDevice()->debugErrorCount() == 0);
    return {engine.culledIndoors(), engine.culledIndoorShadows()};
}
} // namespace

TEST_CASE("Engine indoor cull GPU: a room's furniture is left out from afar outside, drawn near or inside")
{
    // 25 m in front of the room (+Z), looking at it (yaw 0 looks along -Z).
    const Counts far = culledAt(Vec3(0.0f, 1.7f, 28.0f), 0.0f);
    MESSAGE("far: main ", far.main, ", shadows ", far.shadows);
    CHECK(far.main == 1);    // only the table inside, not the one outside
    CHECK(far.shadows >= 1); // and in the cascades
    // 8 m from the room's box: drawn.
    const Counts near = culledAt(Vec3(0.0f, 1.7f, 11.0f), 0.0f);
    CHECK(near.main == 0);
    CHECK(near.shadows == 0);
    // In the other room (no furniture), far from the first: the camera is inside a room - all drawn.
    const Counts inside = culledAt(Vec3(40.0f, 1.7f, 0.0f), 90.0f);
    CHECK(inside.main == 0);
    CHECK(inside.shadows == 0);
    // Switched off.
    const Counts off = culledAt(Vec3(0.0f, 1.7f, 28.0f), 0.0f, 0.0);
    CHECK(off.main == 0);
    CHECK(off.shadows == 0);
}
