// Heightmap terrain on a real driver (label "gpu"): heights reach the screen, culling and LOD, splat
// layers and holes.

#include "GlFixture.hpp"

#include <g7/render/Camera.hpp>
#include <g7/render/Lighting.hpp>
#include <g7/render/Material.hpp>
#include <g7/render/ShaderLibrary.hpp>
#include <g7/render/Terrain.hpp>
#include <g7/render/TextureUpload.hpp>
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
    std::vector<u16> samples;

    explicit TerrainScene(std::vector<u16> heights = cliffSamples(129)) : samples(std::move(heights))
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

asset::TextureData solidTexture(u32 size, u8 r, u8 g, u8 b)
{
    asset::ImageData image{size, size, {}};
    for (u32 i = 0; i < size * size; ++i)
    {
        image.rgba8.insert(image.rgba8.end(), {r, g, b, 255});
    }
    return asset::textureFromImage(std::move(image), true);
}

/// 9 x 9 weights over the whole terrain: west of the middle layer 0, east of it layer 1.
asset::TextureData westEastSplat()
{
    asset::ImageData image{9, 9, {}};
    for (u32 j = 0; j < 9; ++j)
    {
        for (u32 i = 0; i < 9; ++i)
        {
            const u8 east = i > 4 ? 255 : i == 4 ? 128 : 0;
            image.rgba8.insert(image.rgba8.end(), {static_cast<u8>(255 - east), east, 0, 0});
        }
    }
    return asset::textureFromImage(std::move(image), false);
}

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
    scene.terrain.drawShadow(*scene.gl.device, cascades[0], Vec3(0, 50, 0));
    CHECK(scene.gl.device->debugErrorCount() == 0);
    // Each chunk at the detail of the main pass (welt's rings: a coarser shadow surface lay above the drawn
    // one): close to the eye full detail, far away the coarsest grid.
    scene.terrain.lodDistance = 96.0f;
    scene.terrain.drawShadow(*scene.gl.device, cascades[0], Vec3(0, 2, 0));
    CHECK(scene.terrain.shadowLevels()[0] > 0);
    CHECK(scene.terrain.shadowLevels()[3] == 0);
    scene.terrain.drawShadow(*scene.gl.device, cascades[0], Vec3(0, 5000, 0));
    CHECK(scene.terrain.shadowLevels()[0] == 0);
    CHECK(scene.terrain.shadowLevels()[3] > 0);
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
    CHECK(engine.terrain()->holes().size() == 256u * 256u);
    CHECK(engine.terrain()->isHole(126.0f, -4.0f)); // the pit east of the camp (make_terrain.py)
    CHECK_FALSE(engine.terrain()->isHole(0.0f, 0.0f));
    CHECK(engine.terrain()->heightAt(0.0f, 0.0f) == doctest::Approx(0.0f).epsilon(0.01)); // the camp is flat
    CHECK(engine.terrain()->heightAt(-150.0f, -120.0f) < -5.0f);                          // the basin
    CHECK(engine.runFrame());
    CHECK(engine.visibleTerrainChunks() > 0);
    CHECK(engine.renderDevice()->debugErrorCount() == 0);

    // Collision (M5): the terrain, its holes and the camp's walls.
    const physics::PhysicsWorld& collision = engine.physics();
    CHECK(collision.stats().bodies > 1);
    const auto ground = collision.raycast(Vec3(-150.0f, 100.0f, -120.0f), Vec3(0, -1, 0), 500.0f);
    REQUIRE(ground.has_value());
    CHECK(ground->position.y == doctest::Approx(engine.terrain()->heightAt(-150.0f, -120.0f)).epsilon(0.02));
    CHECK(ground->userData == 0);
    CHECK_FALSE(
        collision.raycast(Vec3(126.0f, 100.0f, -4.0f), Vec3(0, -1, 0), 500.0f).has_value()); // the pit
    // Vob 1, a wooden wall at x 13 facing z: a ray along z at knee height hits a mesh vob.
    const auto wall = collision.raycast(Vec3(13.5f, 1.0f, -20.0f), Vec3(0, 0, 1), 40.0f);
    REQUIRE(wall.has_value());
    CHECK(wall->userData != 0);
}

TEST_CASE("Terrain GPU: texture arrays hold equal layers, differences are errors")
{
    GlFixture gl;
    const asset::TextureData red = solidTexture(4, 255, 0, 0);
    const asset::TextureData blue = solidTexture(4, 0, 0, 255);
    const asset::TextureData small = solidTexture(2, 0, 255, 0);
    const std::vector<const asset::TextureData*> layers{&red, &blue};
    Texture array = require(createTextureArray(*gl.device, layers, TextureArrayUsage::Data));
    CHECK(array.desc().layers == 2);
    CHECK(array.desc().isArray());
    CHECK(array.desc().mipLevels == 3); // generated: 4x4, 2x2, 1x1
    const auto pixels = gl.device->readTexture(array, 0);
    REQUIRE(pixels.size() == 2u * 4u * 4u * 4u);
    CHECK(pixels[0] == 255);             // layer 0 red
    CHECK(pixels[4 * 4 * 4 + 2] == 255); // layer 1 blue
    CHECK(require(createTextureArray(*gl.device, std::vector<const asset::TextureData*>{&red},
                                     TextureArrayUsage::Colour))
              .desc()
              .isArray()); // one layer stays an array (sampler2DArray)

    const std::vector<const asset::TextureData*> mixed{&red, &small};
    auto wrongSize = createTextureArray(*gl.device, mixed, TextureArrayUsage::Colour);
    REQUIRE_FALSE(wrongSize.ok());
    CHECK(wrongSize.error().message ==
          "texture array: layer 1 is 2x2, layer 0 is 4x4 (all layers need the same size)");
    asset::TextureData compressed = red;
    compressed.format = asset::TextureFormat::BC7;
    const std::vector<const asset::TextureData*> formats{&red, &compressed};
    auto wrongFormat = createTextureArray(*gl.device, formats, TextureArrayUsage::Colour);
    REQUIRE_FALSE(wrongFormat.ok());
    CHECK(wrongFormat.error().message.find("layer 1 differs from layer 0 in format") != std::string::npos);
    CHECK(gl.device->debugErrorCount() == 0);
}

TEST_CASE("Terrain GPU: splat weights choose the layer, holes show what lies behind")
{
    TerrainScene scene(std::vector<u16>(129 * 129, 0)); // flat at y = 0
    const asset::TextureData red = solidTexture(4, 220, 20, 20);
    const asset::TextureData blue = solidTexture(4, 20, 20, 220);
    const asset::TextureData weights = westEastSplat();
    TerrainSurfaceDesc surface;
    surface.splatMaps = {&weights};
    surface.layers = {{&red, 4.0f}, {&blue, 4.0f}};
    REQUIRE(scene.terrain.setSurface(*scene.gl.device, surface).ok());
    CHECK(scene.terrain.layerCount() == 2);

    auto pixels = scene.render(lookingDown(Vec3(0, 100, 0)));
    const auto at = [&](int x, int y) { return &pixels[(static_cast<usize>(y) * 32 + x) * 4]; };
    CHECK(at(8, 16)[0] > at(8, 16)[2] + 40);   // west: red layer
    CHECK(at(24, 16)[2] > at(24, 16)[0] + 40); // east: blue layer

    // Holes: 128 x 128 cells, a 32 x 32 block in the middle removed.
    std::vector<u8> holes(128 * 128, 255);
    for (u32 r = 48; r < 80; ++r)
    {
        for (u32 c = 48; c < 80; ++c)
        {
            holes[r * 128 + c] = 0;
        }
    }
    surface.holes = holes;
    REQUIRE(scene.terrain.setSurface(*scene.gl.device, surface).ok());
    CHECK(scene.terrain.hasHoles());
    pixels = scene.render(lookingDown(Vec3(0, 100, 0)));
    CHECK(at(16, 16)[0] + at(16, 16)[1] + at(16, 16)[2] == 0); // the clear colour through the hole
    CHECK(at(4, 16)[0] > 0);                                   // ground around it
    CHECK(at(4, 16)[0] > at(4, 16)[2] + 40); // still the red layer after the surface was replaced
    CHECK(at(28, 16)[2] > at(28, 16)[0] + 40);

    // The shadow pass cuts the same holes, without errors.
    ShadowSettings settings;
    settings.resolution = 256;
    ShadowMap shadows = require(ShadowMap::create(*scene.gl.device, settings));
    const auto cascades = computeCascades(lookingDown(Vec3(0, 50, 0)), Vec3(0.3f, 1.0f, 0.2f), settings);
    shadows.begin(*scene.gl.device);
    shadows.beginCascade(*scene.gl.device, 0);
    scene.terrain.drawShadow(*scene.gl.device, cascades[0], Vec3(0, 50, 0));
    CHECK(scene.gl.device->debugErrorCount() == 0);
}

TEST_CASE("Terrain GPU: a broken surface is an error that keeps the previous one")
{
    TerrainScene scene;
    const asset::TextureData red = solidTexture(4, 220, 20, 20);
    const asset::TextureData small = solidTexture(2, 20, 20, 220);
    const asset::TextureData weights = westEastSplat();
    TerrainSurfaceDesc good;
    good.splatMaps = {&weights};
    good.layers = {{&red, 4.0f}};
    REQUIRE(scene.terrain.setSurface(*scene.gl.device, good).ok());

    const auto failure = [&](const TerrainSurfaceDesc& surface)
    {
        auto result = scene.terrain.setSurface(*scene.gl.device, surface);
        REQUIRE_FALSE(result.ok());
        CHECK(scene.terrain.layerCount() == 1); // unchanged
        return result.error().message;
    };
    TerrainSurfaceDesc nine = good;
    nine.layers.assign(9, {&red, 4.0f});
    nine.splatMaps = {&weights, &weights};
    CHECK(failure(nine) == "terrain surface: 9 layers, at most 8");
    TerrainSurfaceDesc fewMaps = good;
    fewMaps.layers.assign(5, {&red, 4.0f});
    CHECK(failure(fewMaps) == "terrain surface: 5 layers need 2 splat map(s), got 1");
    TerrainSurfaceDesc sizes = good;
    sizes.layers = {{&red, 4.0f}, {&small, 4.0f}};
    CHECK(failure(sizes) ==
          "terrain layers: texture array: layer 1 is 2x2, layer 0 is 4x4 (all layers need the same size)");
    TerrainSurfaceDesc tile = good;
    tile.layers[0].tile = 0.0f;
    CHECK(failure(tile) == "terrain surface: layer 0 needs a positive tile size");
    const std::vector<u8> shortMask(10, 255);
    TerrainSurfaceDesc holes = good;
    holes.holes = shortMask;
    CHECK(failure(holes) ==
          "terrain surface: hole mask has 10 bytes, 128 x 128 = 16384 expected (one per cell)");
    CHECK(scene.gl.device->debugErrorCount() == 0);
}
