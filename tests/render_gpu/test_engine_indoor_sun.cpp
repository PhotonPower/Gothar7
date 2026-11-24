// Sun indoors (render.md "Innenräume", label "gpu"): with the camera in a room the far shadow cascades are
// dropped, not shaded with a tile never drawn, and beyond indoor_shadow_distance the rooms get no sun - at
// noon a cellar under a roof stays dark, as dark as without the indoor limits (render.indoor_view_distance =
// 0), while the floor outside lies in the sun.

#include "GlFixture.hpp"
#include "RunTempDir.hpp"

#include <g7/asset/ImageData.hpp>
#include <g7/runtime/Engine.hpp>

#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <string>

using namespace g7;

namespace
{
/// A wide floor and, over the cellar's zone at the origin, a roof (a flat table, top about 3.5 m high, 45 m
/// across) - no windows.
std::filesystem::path writeCellar()
{
    const std::filesystem::path world = test::runTempDir() / "g7_indoor_sun.g7world";
    std::ofstream out(world, std::ios::binary);
    out << R"({"version":1,"name":"indoor_sun","nextVobId":4,"vobs":[)"
        << R"({"id":1,"type":"mesh","name":"FLOOR","pos":[0.0,-0.75,0.0],"rot":[0.0,0.0,0.0,1.0],)"
        << R"("scale":[200.0,1.0,200.0],"mesh":"mobs/table.glb"},)"
        << R"({"id":2,"type":"mesh","name":"ROOF","pos":[0.0,2.75,0.0],"rot":[0.0,0.0,0.0,1.0],)"
        << R"("scale":[30.0,1.0,30.0],"mesh":"mobs/table.glb"},)"
        << R"({"id":3,"type":"start","name":"START","pos":[0.0,0.1,-80.0],"rot":[0.0,0.0,0.0,1.0]}],)"
        << R"("zones":[)"
        << R"({"type":"indoor","value":"WEINKELLER","box":{"center":[0.0,1.5,0.0],"halfExtents":[4.0,1.5,4.0],"yaw":0.0}}]})";
    return world;
}

/// Mean brightness (0..255, r+g+b over 3) of the middle of the frame with the camera 1.7 m above `floor`
/// looking down at noon with the sun; `indoorViewDistance` 0 switches the indoor limits off.
f32 brightnessAt(const std::filesystem::path& world, const Vec3& floor, f64 indoorViewDistance,
                 f64 indoorShadowDistance = 12.0)
{
    EngineConfig config;
    config.appName = "indoor_sun";
    config.window.size = {160, 90};
    config.window.vsync = false;
    config.world = world;
    config.start = "START";
    config.startTime = "12:00";
    config.settings.set<f64>("render.indoor_view_distance", indoorViewDistance);
    config.settings.set<f64>("render.indoor_shadow_distance", indoorShadowDistance);
    config.settings.set<bool>("render.bloom", false);
    config.shaderDirectory = fs::fromUtf8(G7_SHADER_DIR);
    config.fixedFrameSeconds = 1.0 / 60.0;
    config.view.camera = floor + Vec3(0.0f, 1.7f, 0.0f);
    config.view.pitchDegrees = -85.0f;
    config.view.fly = true;
    test::keepVideoAlive();
    Engine engine(config);
    REQUIRE(engine.init().ok());
    for (int i = 0; i < 4; ++i)
    {
        REQUIRE(engine.runFrame());
    }
    const fs::Path file = test::runTempDir() / "g7_indoor_sun.png";
    engine.requestScreenshot(file);
    REQUIRE(engine.runFrame());
    CHECK(engine.renderDevice()->debugErrorCount() == 0);
    auto image = asset::loadImage(file);
    REQUIRE(image.ok());
    const asset::ImageData& img = image.value();
    f64 sum = 0.0;
    usize count = 0;
    for (u32 y = img.height / 4; y < img.height * 3 / 4; ++y)
    {
        for (u32 x = img.width / 4; x < img.width * 3 / 4; ++x)
        {
            const usize i = (static_cast<usize>(y) * img.width + x) * 4;
            sum += (img.rgba8[i] + img.rgba8[i + 1] + img.rgba8[i + 2]) / 3.0;
            ++count;
        }
    }
    std::filesystem::remove(file);
    return static_cast<f32>(sum / static_cast<f64>(count));
}
} // namespace

TEST_CASE("Engine indoor sun GPU: at noon the cellar under its roof stays dark with the camera inside")
{
    const std::filesystem::path world = writeCellar();
    const f32 outside = brightnessAt(world, Vec3(60.0f, 0.0f, 0.0f), 30.0);
    const f32 inside = brightnessAt(world, Vec3(0.0f, 0.0f, 0.0f), 30.0);
    const f32 unlimited = brightnessAt(world, Vec3(0.0f, 0.0f, 0.0f), 0.0);
    MESSAGE("outside ", outside, ", cellar ", inside, ", cellar without indoor limits ", unlimited);
    CHECK(inside < outside * 0.3f);          // the roof's shadow, not the sun
    CHECK(inside < unlimited * 1.2f + 2.0f); // as dark as without the indoor limits

    // Beyond indoor_shadow_distance (here nearer than the floor) the room gets no sun - not full sun where
    // the shadows end, as outside.
    const f32 beyond = brightnessAt(world, Vec3(0.0f, 0.0f, 0.0f), 30.0, 0.5);
    MESSAGE("cellar beyond the shadows ", beyond);
    CHECK(beyond < unlimited * 1.2f + 2.0f);
    std::filesystem::remove(world);
}
