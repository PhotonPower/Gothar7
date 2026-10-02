// The M2 test scene (DoD) through the whole engine on a real driver (label "gpu"): it loads, renders
// without GL errors and frustum culling skips what is out of view.

#include <g7/render/Device.hpp>
#include <g7/runtime/Engine.hpp>

#include <doctest/doctest.h>

using namespace g7;

namespace
{
EngineConfig sceneConfig(u32 viewpoint)
{
    EngineConfig config;
    config.appName = "test scene";
    config.window.size = {320, 180};
    config.window.vsync = false;
    config.scene = fs::fromUtf8("testscene/scene.toml"); // VFS path: development mount of assets/source
    config.viewpoint = viewpoint;
    config.shaderDirectory = fs::fromUtf8(G7_SHADER_DIR);
    config.fixedFrameSeconds = 1.0 / 60.0;
    return config;
}
} // namespace

TEST_CASE("Test scene: loads, renders and culls")
{
    // Viewpoint 1 is at eye level between the huts: much of the camp lies behind or beside it.
    Engine engine(sceneConfig(1));
    auto result = engine.init();
    REQUIRE_MESSAGE(result.ok(), (result.ok() ? "" : result.error().message));
    REQUIRE(engine.renderDevice() != nullptr);
    CHECK(engine.sceneObjectCount() > 100);
    for (int i = 0; i < 3; ++i)
    {
        CHECK(engine.runFrame());
    }
    CHECK(engine.visibleSceneObjects() > 10);
    CHECK(engine.visibleSceneObjects() < engine.sceneObjectCount());
    CHECK(engine.renderDevice()->stats().drawCalls > 10);
    CHECK(engine.renderDevice()->debugErrorCount() == 0);
}

TEST_CASE("Test scene: an unknown viewpoint falls back to the first")
{
    Engine engine(sceneConfig(99));
    REQUIRE(engine.init().ok());
    CHECK(engine.runFrame());
    CHECK(engine.visibleSceneObjects() > 0);
}

TEST_CASE("Test scene: a broken scene file fails init with its name")
{
    EngineConfig config = sceneConfig(0);
    config.scene = fs::fromUtf8("does/not/exist.toml");
    Engine engine(std::move(config));
    auto result = engine.init();
    REQUIRE_FALSE(result.ok());
    CHECK(result.error().message.find("exist.toml") != std::string::npos);
}
