// The engine loads models through its VFS and asset manager (label "gpu").

#include <g7/asset/ImageData.hpp>
#include <g7/render/Device.hpp>
#include <g7/runtime/Engine.hpp>

#include <doctest/doctest.h>

#include <chrono>
#include <filesystem>
#include <string>

using namespace g7;

namespace
{
EngineConfig viewConfig(const fs::Path& viewMesh)
{
    EngineConfig config;
    config.appName = "engine assets";
    config.window.size = {160, 90};
    config.window.vsync = false;
    config.viewMesh = viewMesh;
    config.shaderDirectory = fs::fromUtf8(G7_SHADER_DIR);
    config.fixedFrameSeconds = 1.0 / 60.0;
    return config;
}

const render::Material& firstMaterial(const Engine& engine, std::string_view path)
{
    const LoadedModel* model = engine.model(path);
    REQUIRE(model != nullptr);
    REQUIRE(model->materials.size() > 0);
    return model->materials[0];
}
} // namespace

TEST_CASE("Engine assets: a model from the development mount with its texture")
{
    Engine engine(viewConfig(fs::fromUtf8("testscene/town/wall.glb")));
    auto result = engine.init();
    REQUIRE_MESSAGE(result.ok(), (result.ok() ? "" : result.error().message));
    // Kenney's GLBs reference Textures/colormap.png next to them (512 x 512).
    const render::Material& material = firstMaterial(engine, "testscene/town/wall.glb");
    REQUIRE(material.baseColor != nullptr);
    CHECK(material.baseColor->desc().width == 512);
    CHECK(engine.runFrame());
    CHECK(engine.renderDevice()->debugErrorCount() == 0);
}

TEST_CASE("Engine assets: a file outside the mounts is mounted under local/")
{
    const fs::Path file = fs::fromUtf8(G7_TESTSCENE).parent_path() / "town" / "lantern.glb";
    Engine engine(viewConfig(file));
    REQUIRE(engine.init().ok());
    // Not resolved through the development mount: the argument is a disk path.
    const render::Material& material = firstMaterial(engine, "local/lantern.glb");
    CHECK(material.baseColor->desc().width == 512); // the texture next to it resolved as well
}

TEST_CASE("Engine assets: a mount with higher priority overrides a texture")
{
    // Override folder with a 2x2 red "colormap" at the same VFS path.
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    const fs::Path root = std::filesystem::temp_directory_path() / ("g7_override_" + std::to_string(stamp));
    std::filesystem::create_directories(root / "testscene" / "town" / "Textures");
    asset::ImageData red{2, 2, {}};
    for (int i = 0; i < 4; ++i)
    {
        red.rgba8.insert(red.rgba8.end(), {255, 0, 0, 255});
    }
    REQUIRE(asset::savePng(root / "testscene" / "town" / "Textures" / "colormap.png", red).ok());

    EngineConfig config = viewConfig(fs::fromUtf8("testscene/town/wall.glb"));
    auto settings =
        Config::parse("[[assets.mount]]\npath = \"" + root.generic_string() + "\"\npriority = 50\n");
    REQUIRE(settings.ok());
    config.settings = std::move(settings).value();
    {
        Engine engine(std::move(config));
        REQUIRE(engine.init().ok());
        const render::Material& material = firstMaterial(engine, "testscene/town/wall.glb");
        CHECK(material.baseColor->desc().width == 2);
        CHECK(engine.renderDevice()->debugErrorCount() == 0);
    }
    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
}

TEST_CASE("Engine assets: unknown models fail init with a clear message")
{
    Engine engine(viewConfig(fs::fromUtf8("testscene/town/no-such-model.glb")));
    auto result = engine.init();
    REQUIRE_FALSE(result.ok());
    CHECK(result.error().message.find("no-such-model.glb") != std::string::npos);
}
