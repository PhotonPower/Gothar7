// Indoors (render.md "Innenräume", label "gpu"): with the camera in a room of Leonberg the frame draws only
// what is within indoor_view_distance, shadows from casters within indoor_shadow_distance - far fewer
// triangles than the same camera just outside. Needs the generated town (not versioned; skipped without).

#include "GlFixture.hpp"

#include <g7/runtime/Engine.hpp>

#include <doctest/doctest.h>

#include <filesystem>
#include <string>

using namespace g7;

namespace
{
/// Triangles of the 5th frame with the free camera at `camera` looking along `yawDegrees`.
u64 trianglesAt(const Vec3& camera, f32 yawDegrees)
{
    EngineConfig config;
    config.appName = "indoor";
    config.window.size = {320, 180};
    config.window.vsync = false;
    config.world = fs::fromUtf8("worlds/leonberg/leonberg.g7world");
    config.startTime = "12:00";
    config.shaderDirectory = fs::fromUtf8(G7_SHADER_DIR);
    config.fixedFrameSeconds = 1.0 / 60.0;
    config.view.camera = camera;
    config.view.yawDegrees = yawDegrees;
    config.view.pitchDegrees = -10.0f;
    config.view.fly = true;
    test::keepVideoAlive();
    Engine engine(config);
    REQUIRE(engine.init().ok());
    REQUIRE(engine.renderDevice() != nullptr);
    for (int i = 0; i < 5; ++i)
    {
        REQUIRE(engine.runFrame());
    }
    return engine.renderDevice()->stats().triangles;
}
} // namespace

TEST_CASE(
    "Engine indoor GPU: in the inn's parlour the frame draws only the near world (with the generated town)")
{
    if (!std::filesystem::exists(std::string(G7_ASSET_SOURCE_DIR) +
                                 "/worlds/leonberg/generated/leonberg_terrain.r16"))
    {
        MESSAGE("skipped: Leonberg's generated files are not here");
        return;
    }
    // The parlour of the Krumme Gans (zone LEO_GASTHAUS_ZHE_INNEN) and a point in the lane 12 m in front of
    // it.
    const u64 inside = trianglesAt(Vec3(-89.7f, 1.4f, 27.0f), 0.0f);
    const u64 outside = trianglesAt(Vec3(-93.0f, 1.8f, 38.0f), 0.0f);
    MESSAGE("triangles inside ", inside, ", outside ", outside);
    CHECK(inside < 500000);
    CHECK(inside * 2 < outside);
}
