// HDR target, post pass and fog on a real driver (label "gpu").

#include "GlFixture.hpp"

#include <g7/asset/Procedural.hpp>
#include <g7/render/Bloom.hpp>
#include <g7/render/Camera.hpp>
#include <g7/render/Grading.hpp>
#include <g7/render/Lighting.hpp>
#include <g7/render/Material.hpp>
#include <g7/render/Mesh.hpp>
#include <g7/render/PassTimes.hpp>
#include <g7/render/PostProcess.hpp>
#include <g7/render/ShaderLibrary.hpp>

#include <glm/gtc/matrix_transform.hpp>

#include <cmath>

using namespace g7;
using namespace g7::render;
using namespace g7::render::rhi;
using g7::test::GlFixture;
using g7::test::require;

namespace
{
Vec4 centreOf(Device& device, const SceneTarget& target)
{
    const auto pixels = device.readTextureFloat(target.color(), 0);
    REQUIRE(pixels.size() == static_cast<usize>(target.width()) * target.height() * 4);
    const usize i = (static_cast<usize>(target.height() / 2) * target.width() + target.width() / 2) * 4;
    return Vec4(pixels[i], pixels[i + 1], pixels[i + 2], pixels[i + 3]);
}
} // namespace

TEST_CASE("Post: the HDR scene target keeps values above 1 and follows resizes")
{
    GlFixture gl;
    SceneTarget target = require(SceneTarget::create(*gl.device, 8, 8));
    gl.device->bindFramebuffer(&target.framebuffer());
    gl.device->setViewport(0, 0, 8, 8);
    gl.device->clear(Vec4(4.0f, 0.5f, 0.0f, 1.0f), 0.0f);
    const Vec4 c = centreOf(*gl.device, target);
    CHECK(c.x == doctest::Approx(4.0f));
    CHECK(c.y == doctest::Approx(0.5f));

    REQUIRE(target.resize(*gl.device, 16, 4).ok());
    CHECK(target.width() == 16);
    CHECK(target.height() == 4);
    REQUIRE(target.resize(*gl.device, 0, 0).ok()); // minimised window: kept at 1x1, never 0
    CHECK(target.width() == 1);
    CHECK(gl.device->debugErrorCount() == 0);
}

TEST_CASE("Post: exposure, tonemapping and sRGB encoding")
{
    GlFixture gl;
    ShaderLibrary library(*gl.device, fs::fromUtf8(G7_SHADER_DIR));
    PostProcess post = require(PostProcess::create(*gl.device, library));
    SceneTarget scene = require(SceneTarget::create(*gl.device, 8, 8));
    Texture output = require(gl.device->createTexture({8, 8, Format::RGBA8, 1}));
    Framebuffer window = require(gl.device->createFramebuffer({{&output}, nullptr}));

    const auto run = [&](f32 value, const PostSettings& settings)
    {
        gl.device->bindFramebuffer(&scene.framebuffer());
        gl.device->setViewport(0, 0, 8, 8);
        gl.device->clear(Vec4(value, value, value, 1.0f), 0.0f);
        gl.device->bindFramebuffer(&window);
        post.apply(*gl.device, scene, 8, 8, settings);
        return int(gl.device->readPixels(4, 4, 1, 1, &window)[0]);
    };
    // Linear 0.18 (middle grey) -> sRGB 118 without tonemapping (±1 for the dither).
    CHECK(std::abs(run(0.18f, {Tonemapper::None, 1.0f}) - 118) <= 1);
    CHECK(run(4.0f, {Tonemapper::None, 1.0f}) == 255); // clipped
    const int aces = run(4.0f, {Tonemapper::Aces, 1.0f});
    CHECK(aces < 255);                                 // highlight rolls off
    CHECK(aces > run(1.0f, {Tonemapper::Aces, 1.0f})); // but stays brighter
    CHECK(run(0.09f, {Tonemapper::None, 2.0f}) == run(0.18f, {Tonemapper::None, 1.0f})); // exposure
    CHECK(gl.device->debugErrorCount() == 0);
}

TEST_CASE("Post: distance fog towards the fog colour")
{
    GlFixture gl;
    ShaderLibrary library(*gl.device, fs::fromUtf8(G7_SHADER_DIR));
    MeshRenderer renderer = require(MeshRenderer::create(*gl.device, library, 1.0f));
    SceneTarget scene = require(SceneTarget::create(*gl.device, 16, 16));

    // Big white plate facing the camera; ambient 1 makes the lit colour exactly white.
    asset::MeshData plate = asset::makePlane(4000.0f);
    Mesh mesh = require(Mesh::create(*gl.device, plate));
    MaterialSet materials = require(MaterialSet::create(*gl.device, plate, MaterialSet::ImageLookup{}));
    Environment environment{.sunIntensity = 0.0f, .ambientSky = Vec3(1.0f), .ambientGround = Vec3(1.0f)};
    environment.fogColor = Vec3(0.2f, 0.4f, 0.6f);
    environment.fogStart = 30.0f;
    environment.fogDensity = fogDensityFor(0.9f, 300.0f, 30.0f);
    const LightList none;

    const auto centreAt = [&](f32 distance)
    {
        Camera camera;
        camera.aspect = 1.0f;
        camera.farPlane = 5000.0f;
        // Plate rotated upright (normal +Z) at -distance in front of the camera.
        const Mat4 model = glm::translate(Mat4(1.0f), Vec3(0, 0, -distance)) *
                           glm::rotate(Mat4(1.0f), toRadians(90.0f), Vec3(1, 0, 0));
        gl.device->bindFramebuffer(&scene.framebuffer());
        gl.device->setViewport(0, 0, 16, 16);
        gl.device->clear(Vec4(0, 0, 0, 1), 0.0f);
        renderer.setLighting(*gl.device, environment, none);
        renderer.draw(*gl.device, mesh, materials, model, camera);
        return Vec3(centreOf(*gl.device, scene));
    };
    CHECK(nearlyEqual(centreAt(10.0f), Vec3(1.0f), 1e-2f)); // before the fog start
    CHECK(nearlyEqual(centreAt(300.0f), glm::mix(Vec3(1.0f), environment.fogColor, 0.9f), 2e-2f));
    CHECK(nearlyEqual(centreAt(1500.0f), environment.fogColor, 1e-2f)); // fully fogged
    CHECK(gl.device->debugErrorCount() == 0);
}

TEST_CASE("Post: bloom spreads a bright pixel, darker scenes do not glow, intensity 0 changes nothing (M17)")
{
    GlFixture gl;
    ShaderLibrary library(*gl.device, fs::fromUtf8(G7_SHADER_DIR));
    PostProcess post = require(PostProcess::create(*gl.device, library));
    Bloom bloom = require(Bloom::create(*gl.device, library));
    SceneTarget scene = require(SceneTarget::create(*gl.device, 64, 64));
    REQUIRE(bloom.resize(*gl.device, 64, 64, 4).ok());
    BloomSettings settings;
    settings.threshold = 1.0f;
    settings.intensity = 1.0f;

    // Dark grey (below the threshold and its knee): no glow.
    gl.device->bindFramebuffer(&scene.framebuffer());
    gl.device->setViewport(0, 0, 64, 64);
    gl.device->clear(Vec4(0.3f, 0.3f, 0.3f, 1.0f), 0.0f);
    bloom.render(*gl.device, scene.color(), settings);
    REQUIRE(bloom.result() != nullptr);
    const auto dark = gl.device->readTextureFloat(*bloom.result(), 0);
    f32 darkMax = 0.0f;
    for (usize i = 0; i < dark.size(); ++i)
    {
        if (i % 4 != 3) // colour, not alpha (the additive up steps add up alpha)
        {
            darkMax = std::max(darkMax, dark[i]);
        }
    }
    CHECK(darkMax < 1e-4f);

    // A bright square in the middle: the glow reaches beyond it.
    gl.device->bindFramebuffer(&scene.framebuffer());
    gl.device->clear(Vec4(0.0f, 0.0f, 0.0f, 1.0f), 0.0f);
    gl.device->setScissor(PixelRect{30, 30, 4, 4});
    gl.device->clear(Vec4(50.0f, 50.0f, 50.0f, 1.0f), std::nullopt);
    gl.device->setScissor(std::nullopt);
    bloom.render(*gl.device, scene.color(), settings);
    const rhi::Texture& glow = *bloom.result();
    const auto pixels = gl.device->readTextureFloat(glow, 0);
    const u32 w = glow.desc().width;
    const auto at = [&](u32 x, u32 y) { return pixels[(static_cast<usize>(y) * w + x) * 4]; };
    CHECK(w == 32);
    CHECK(at(16, 16) > 0.5f);       // over the square (half size: 15..17)
    CHECK(at(16 + 5, 16) > 0.001f); // spread around it
    CHECK(at(16 + 5, 16) < at(16, 16));
    CHECK(at(1, 1) < at(16 + 5, 16)); // far away: less

    // The post pass: bloom adds light near the square; intensity 0 gives the bytes of no bloom.
    Texture output = require(gl.device->createTexture({64, 64, Format::RGBA8, 1}));
    Framebuffer window = require(gl.device->createFramebuffer({{&output}, nullptr}));
    const auto shot = [&](const PostSettings& s)
    {
        gl.device->bindFramebuffer(&window);
        post.apply(*gl.device, scene, 64, 64, s);
        return gl.device->readPixels(0, 0, 64, 64, &window);
    };
    const auto plain = shot({Tonemapper::Aces, 1.0f, nullptr, 0.0f});
    const auto off = shot({Tonemapper::Aces, 1.0f, &glow, 0.0f});
    const auto on = shot({Tonemapper::Aces, 1.0f, &glow, 0.5f});
    CHECK(off == plain);
    const usize near = (static_cast<usize>(32) * 64 + 40) * 4; // 8 px right of the square
    CHECK(int(on[near]) > int(plain[near]) + 5);
    CHECK(gl.device->debugErrorCount() == 0);
}

TEST_CASE("Post: GPU timer queries measure a pass (M17)")
{
    GlFixture gl;
    PassTimes times;
    SceneTarget scene = require(SceneTarget::create(*gl.device, 256, 256));
    for (int frame = 0; frame < 12; ++frame)
    {
        times.collect(*gl.device);
        times.begin(*gl.device, "clear");
        gl.device->bindFramebuffer(&scene.framebuffer());
        gl.device->setViewport(0, 0, 256, 256);
        gl.device->clear(Vec4(0.2f, 0.2f, 0.2f, 1.0f), 0.0f);
        times.end(*gl.device);
        (void)gl.device->readPixels(0, 0, 1, 1, &scene.framebuffer()); // waits for the GPU
    }
    times.collect(*gl.device);
    REQUIRE(times.times().size() == 1);
    CHECK(times.times()[0].name == "clear");
    CHECK(times.milliseconds("clear") > 0.0f);
    CHECK(times.milliseconds("nothing") == 0.0f);
    CHECK(gl.device->debugErrorCount() == 0);
}

TEST_CASE("Post: colour grading - neutral changes (almost) nothing, saturation 0 is grey (M17)")
{
    GlFixture gl;
    ShaderLibrary library(*gl.device, fs::fromUtf8(G7_SHADER_DIR));
    PostProcess post = require(PostProcess::create(*gl.device, library));
    SceneTarget scene = require(SceneTarget::create(*gl.device, 8, 8));
    Texture output = require(gl.device->createTexture({8, 8, Format::RGBA8, 1}));
    Framebuffer window = require(gl.device->createFramebuffer({{&output}, nullptr}));
    gl.device->bindFramebuffer(&scene.framebuffer());
    gl.device->setViewport(0, 0, 8, 8);
    gl.device->clear(Vec4(0.6f, 0.2f, 0.1f, 1.0f), 0.0f);
    const auto shot = [&](std::optional<Grading> grading)
    {
        gl.device->bindFramebuffer(&window);
        PostSettings settings{Tonemapper::Aces, 1.0f, nullptr, 0.0f, grading};
        post.apply(*gl.device, scene, 8, 8, settings);
        return gl.device->readPixels(4, 4, 1, 1, &window);
    };
    const auto plain = shot(std::nullopt);
    const auto neutral = shot(Grading{});
    for (int i = 0; i < 3; ++i)
    {
        CHECK(std::abs(int(plain[i]) - int(neutral[i])) <= 1);
    }
    Grading grey;
    grey.saturation = 0.0f;
    const auto g = shot(grey);
    CHECK(std::abs(int(g[0]) - int(g[1])) <= 1);
    CHECK(std::abs(int(g[1]) - int(g[2])) <= 1);
    Grading warm;
    warm.temperature = 1.0f;
    const auto w = shot(warm);
    CHECK(int(w[0]) - int(w[2]) > int(plain[0]) - int(plain[2])); // redder
    CHECK(gl.device->debugErrorCount() == 0);
}
