// The engine loads models through its VFS and asset manager (label "gpu").

#include "GlFixture.hpp"

#include <g7/asset/ImageData.hpp>
#include <g7/render/Device.hpp>
#include <g7/runtime/Engine.hpp>

#include <doctest/doctest.h>

#include <chrono>
#include <filesystem>
#include <string>
#include <thread>

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
    g7::test::keepVideoAlive();
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
    g7::test::keepVideoAlive();
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
        g7::test::keepVideoAlive();
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
    g7::test::keepVideoAlive();
    Engine engine(viewConfig(fs::fromUtf8("testscene/town/no-such-model.glb")));
    auto result = engine.init();
    REQUIRE_FALSE(result.ok());
    CHECK(result.error().message.find("no-such-model.glb") != std::string::npos);
}

TEST_CASE("Engine assets: hot reload shows a changed texture without a restart")
{
    // A 2x2 texture in an override folder; later replaced by a 4x4 one.
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    const fs::Path root = std::filesystem::temp_directory_path() / ("g7_hotreload_" + std::to_string(stamp));
    const fs::Path texture = root / "testscene" / "town" / "Textures" / "colormap.png";
    std::filesystem::create_directories(texture.parent_path());
    const auto solid = [](u32 size, u8 red)
    {
        asset::ImageData image{size, size, {}};
        for (u32 i = 0; i < size * size; ++i)
        {
            image.rgba8.insert(image.rgba8.end(), {red, 0, 0, 255});
        }
        return image;
    };
    REQUIRE(asset::savePng(texture, solid(2, 255)).ok());

    EngineConfig config = viewConfig(fs::fromUtf8("testscene/town/wall.glb"));
    auto settings = Config::parse("[assets]\nhot_reload = true\n[[assets.mount]]\npath = \"" +
                                  root.generic_string() + "\"\npriority = 50\n");
    REQUIRE(settings.ok());
    config.settings = std::move(settings).value();
    {
        g7::test::keepVideoAlive();
        Engine engine(std::move(config));
        REQUIRE(engine.init().ok());
        CHECK(engine.assets().hotReload());
        const LoadedModel* model = engine.model("testscene/town/wall.glb");
        REQUIRE(model != nullptr);
        CHECK(model->materials[0].baseColor->desc().width == 2);
        CHECK(engine.runFrame());

        REQUIRE(asset::savePng(texture, solid(4, 128)).ok());
        // Make the change visible to the timestamp check regardless of the file system's resolution.
        std::error_code ec;
        std::filesystem::last_write_time(
            texture, std::filesystem::last_write_time(texture, ec) + std::chrono::seconds(2), ec);
        // The engine polls every 0.5 s (real time); give it up to 5 s.
        for (int i = 0; i < 100 && model->materials[0].baseColor->desc().width != 4; ++i)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            CHECK(engine.runFrame());
        }
        CHECK(engine.model("testscene/town/wall.glb") == model); // same model, new texture
        CHECK(model->materials[0].baseColor->desc().width == 4);
        CHECK(model->sourceVersion == 1);
        CHECK(engine.renderDevice()->debugErrorCount() == 0);
    }
    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
}
