// Daylight per room (world.md "Zonen", label "gpu"): at noon a windowless cellar (environment.toml [indoor]
// ambient_windowless) is darker than a hall with a window light ([indoor] ambient), both darker than outside;
// a zone's own `daylight` replaces the derived factor.

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
/// Two rooms side by side on one floor, no walls: the hall (x 0) with a window light that lights nothing, the
/// cellar (x 20) without; `cellarDaylight` < 0: derived, else written as the cellar's own daylight.
std::filesystem::path writeRooms(f32 cellarDaylight)
{
    const std::filesystem::path world = test::runTempDir() / "g7_indoor_daylight.g7world";
    std::ofstream out(world, std::ios::binary);
    out << R"({"version":1,"name":"daylight","nextVobId":4,"vobs":[)"
        << R"({"id":1,"type":"mesh","name":"FLOOR","pos":[0.0,-0.75,0.0],"rot":[0.0,0.0,0.0,1.0],)"
        << R"("scale":[200.0,1.0,200.0],"mesh":"mobs/table.glb"},)"
        << R"({"id":2,"type":"start","name":"START","pos":[0.0,0.1,-40.0],"rot":[0.0,0.0,0.0,1.0]},)"
        << R"({"id":3,"type":"light","name":"WINDOW","pos":[3.9,2.5,3.9],"rot":[0.0,0.0,0.0,1.0],)"
        << R"("components":{"light":{"color":[1.0,1.0,1.0],"range":0.2,"intensity":0.001,"daylight":true}}}],)"
        << R"("zones":[)"
        << R"({"type":"indoor","value":"HALLE","box":{"center":[0.0,1.5,0.0],"halfExtents":[4.0,1.5,4.0],"yaw":0.0}},)"
        << R"({"type":"indoor","value":"WEINKELLER","box":{"center":[20.0,1.5,0.0],"halfExtents":[4.0,1.5,4.0],"yaw":0.0})";
    if (cellarDaylight >= 0.0f)
    {
        out << R"(,"daylight":)" << cellarDaylight;
    }
    out << "}]}";
    return world;
}

/// Mean brightness (0..255, r+g+b over 3) of the middle of the frame with the camera 1.7 m above `floor`
/// looking down, at noon without sun (only the ambient and the lights), exposure 8.
f32 brightnessAt(const std::filesystem::path& world, const Vec3& floor)
{
    EngineConfig config;
    config.appName = "daylight";
    config.window.size = {160, 90};
    config.window.vsync = false;
    config.world = world;
    config.start = "START";
    config.startTime = "12:00";
    config.sun = false;
    config.settings.set<f64>("render.exposure", 8.0); // the ambient alone is dim: brighter to measure it
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
    const fs::Path file = test::runTempDir() / "g7_indoor_daylight.png";
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

TEST_CASE("Engine indoor daylight GPU: at noon the windowless cellar is darker than the hall with a window")
{
    const std::filesystem::path world = writeRooms(-1.0f);
    const f32 outside = brightnessAt(world, Vec3(-20.0f, 0.0f, 0.0f));
    const f32 hall = brightnessAt(world, Vec3(0.0f, 0.0f, 0.0f));
    const f32 cellar = brightnessAt(world, Vec3(20.0f, 0.0f, 0.0f));
    MESSAGE("outside ", outside, ", hall ", hall, ", cellar ", cellar);
    CHECK(hall < outside * 0.85f);
    CHECK(cellar < hall * 0.6f); // 0.06 against 0.35 of the ambient
    CHECK(cellar > 0.0f);        // dark, not black

    // The cellar's own daylight (welt can set it per zone) replaces the derived factor: as bright as the
    // hall.
    const std::filesystem::path own = writeRooms(0.35f);
    const f32 lit = brightnessAt(own, Vec3(20.0f, 0.0f, 0.0f));
    MESSAGE("cellar with daylight 0.35: ", lit);
    CHECK(lit == doctest::Approx(hall).epsilon(0.05));
    std::filesystem::remove(own);
}
