// Heightmap terrain on a real driver (label "gpu"): heights reach the screen, culling and LOD.

#include "GlFixture.hpp"

#include <g7/render/Camera.hpp>
#include <g7/render/Lighting.hpp>
#include <g7/render/Material.hpp>
#include <g7/render/ShaderLibrary.hpp>
#include <g7/render/Terrain.hpp>
#include <g7/runtime/Engine.hpp>

#include <vector>

using namespace g7;
using namespace g7::render;
using namespace g7::render::rhi;
using g7::test::GlFixture;
using g7::test::require;

namespace
{
/// 129 x 129 samples, 1 m apart, centred on the origin: west half at minY (0), east half at maxY (20).
std::vector<u16> cliffSamples(u32 size)
{
    std::vector<u16> samples(static_cast<usize>(size) * size);
    for (u32 r = 0; r < size; ++r)
    {
        for (u32 c = 0; c < size; ++c)
        {
            samples[static_cast<usize>(r) * size + c] = c < size / 2 ? 0 : 65535;
        }
    }
    return samples;
}

struct TerrainScene
{
    GlFixture gl;
    ShaderLibrary library{*gl.device, fs::fromUtf8(G7_SHADER_DIR)};
    MeshRenderer meshes;
    TerrainRenderer terrain;
    Texture color;
    Texture depth;
    Framebuffer target;
    std::vector<u16> samples = cliffSamples(129);

    TerrainScene()
    {
        meshes = require(MeshRenderer::create(*gl.device, library, 1.0f));
        const HeightfieldDesc desc{129, 129, 1.0f, Vec2(-64.0f, -64.0f), 0.0f, 20.0f, samples};
        terrain = require(TerrainRenderer::create(*gl.device, library, desc));
        color = require(gl.device->createTexture({32, 32, Format::RGBA8, 1}));
        depth = require(gl.device->createTexture({32, 32, Format::Depth32F, 1}));
        target = require(gl.device->createFramebuffer({{&color}, &depth}));
    }

    std::vector<u8> render(const Camera& camera)
    {
        gl.device->bindFramebuffer(&target);
        gl.device->setViewport(0, 0, 32, 32);
        gl.device->clear(Vec4(0, 0, 0, 1), 0.0f);
        const Environment light{.sunIntensity = 0.0f, .ambientSky = Vec3(1.0f), .ambientGround = Vec3(1.0f)};
        const LightList none;
        meshes.setLighting(*gl.device, light, none);
        meshes.bindLighting(*gl.device);
        terrain.draw(*gl.device, camera, &none);
        return gl.device->readPixels(0, 0, 32, 32, &target);
    }
};

Camera lookingDown(const Vec3& position)
{
    Camera camera;
    camera.aspect = 1.0f;
    camera.transform.position = position;
    camera.transform.rotation = lookRotation(Vec3(0, -1, 0), Vec3(0, 0, -1));
    return camera;
}
} // namespace

TEST_CASE("Terrain GPU: heights from the texture reach the screen")
{
    TerrainScene scene;
    CHECK(scene.terrain.chunkCount() == 4); // 128 cells / 64 per chunk, squared
    CHECK(scene.terrain.bounds().max == Vec3(64.0f, 20.0f, 64.0f));
    // From 100 m above: the high east half is brighter (the albedo rises with height) and nearer.
    const auto pixels = scene.render(lookingDown(Vec3(0, 100, 0)));
    const auto brightness = [&](int x, int y)
    {
        const usize i = (static_cast<usize>(y) * 32 + x) * 4;
        return int(pixels[i]) + pixels[i + 1] + pixels[i + 2];
    };
    const int west = brightness(8, 16);
    const int east = brightness(24, 16);
    CHECK(west > 0);
    CHECK(east > west + 10);
    CHECK(scene.terrain.drawnChunks() == 4);
    CHECK(scene.gl.device->debugErrorCount() == 0);
}

TEST_CASE("Terrain GPU: chunks outside the view are skipped, distant ones coarser")
{
    TerrainScene scene;
    // Over the north-west chunk, looking steeply down to the north-west: the other chunks lie behind.
    Camera camera;
    camera.aspect = 1.0f;
    camera.transform.position = Vec3(-40.0f, 30.0f, -40.0f);
    camera.transform.rotation = lookRotation(Vec3(-1.0f, -1.5f, -1.0f));
    scene.render(camera);
    CHECK(scene.terrain.drawnChunks() >= 1);
    CHECK(scene.terrain.drawnChunks() < scene.terrain.chunkCount());

    // Far away (within the far plane): every chunk at the coarsest level, still without errors.
    scene.terrain.lodDistance = 1.0f;
    scene.render(lookingDown(Vec3(0, 1000, 0)));
    CHECK(scene.terrain.drawnChunks() == 4);

    // The shadow pass draws into a depth target.
    ShadowSettings settings;
    settings.resolution = 256;
    ShadowMap shadows = require(ShadowMap::create(*scene.gl.device, settings));
    const auto cascades = computeCascades(lookingDown(Vec3(0, 50, 0)), Vec3(0.3f, 1.0f, 0.2f), settings);
    shadows.begin(*scene.gl.device);
    shadows.beginCascade(*scene.gl.device, 0);
    scene.terrain.drawShadow(*scene.gl.device, cascades[0]);
    CHECK(scene.gl.device->debugErrorCount() == 0);
}

TEST_CASE("Terrain GPU: invalid heightfields are rejected")
{
    GlFixture gl;
    ShaderLibrary library(*gl.device, fs::fromUtf8(G7_SHADER_DIR));
    std::vector<u16> four(4);
    CHECK_FALSE(
        TerrainRenderer::create(*gl.device, library, {2, 3, 1.0f, Vec2(0.0f), 0.0f, 1.0f, four}).ok());
    CHECK_FALSE(
        TerrainRenderer::create(*gl.device, library, {2, 2, 0.0f, Vec2(0.0f), 0.0f, 1.0f, four}).ok());
    CHECK_FALSE(
        TerrainRenderer::create(*gl.device, library, {2, 2, 1.0f, Vec2(0.0f), 1.0f, 1.0f, four}).ok());
    CHECK(TerrainRenderer::create(*gl.device, library, {2, 2, 1.0f, Vec2(0.0f), 0.0f, 1.0f, four}).ok());
}

TEST_CASE("Terrain GPU: the test world loads its terrain through the engine")
{
    EngineConfig config;
    config.appName = "terrain";
    config.window.size = {160, 90};
    config.window.vsync = false;
    config.world = fs::fromUtf8("testworld/camp.g7world");
    config.shaderDirectory = fs::fromUtf8(G7_SHADER_DIR);
    config.fixedFrameSeconds = 1.0 / 60.0;
    g7::test::keepVideoAlive();
    Engine engine(std::move(config));
    auto result = engine.init();
    REQUIRE_MESSAGE(result.ok(), (result.ok() ? "" : result.error().message));
    REQUIRE(engine.terrain() != nullptr);
    CHECK(engine.terrain()->heightAt(0.0f, 0.0f) == doctest::Approx(0.0f).epsilon(0.01)); // the camp is flat
    CHECK(engine.terrain()->heightAt(-150.0f, -120.0f) < -5.0f);                          // the basin
    CHECK(engine.runFrame());
    CHECK(engine.visibleTerrainChunks() > 0);
    CHECK(engine.renderDevice()->debugErrorCount() == 0);
}
