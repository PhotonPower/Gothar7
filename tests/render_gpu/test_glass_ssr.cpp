// Screen-space reflections on window glass (M17 windows F3, label "gpu"): with window reflections high the
// glass traces its rays through the previous frame's scene (render::ScreenHistory): a glowing panel on the
// screen shows in the glass below it; where the ray finds nothing the glass stays as with medium (probe and
// sky).

#include "GlFixture.hpp"

#include <g7/render/Camera.hpp>
#include <g7/render/Glass.hpp>
#include <g7/render/Lighting.hpp>
#include <g7/render/Material.hpp>
#include <g7/render/Mesh.hpp>
#include <g7/render/ScreenHistory.hpp>
#include <g7/render/ShaderLibrary.hpp>

#include <vector>

using namespace g7;
using namespace g7::render;
using namespace g7::render::rhi;
using g7::test::GlFixture;
using g7::test::require;

namespace
{
constexpr i32 kSize = 64;

/// An 8 x 8 m quad in the XY plane facing +Z: glass, or a glowing panel.
asset::MeshData quad(bool glass, const Vec3& emissive = Vec3(0.0f))
{
    asset::MeshData data;
    for (const Vec2 p : {Vec2(-4.0f, -4.0f), Vec2(4.0f, -4.0f), Vec2(4.0f, 4.0f), Vec2(-4.0f, 4.0f)})
    {
        asset::Vertex v{};
        v.position = Vec3(p, 0.0f);
        v.normal = Vec3(0.0f, 0.0f, 1.0f);
        data.vertices.push_back(v);
    }
    data.indices = {0, 1, 2, 0, 2, 3};
    data.submeshes = {{0, 6, 0}};
    asset::MaterialInfo material;
    material.baseColor = Vec4(0.0f, 0.0f, 0.0f, 1.0f);
    material.emissive = emissive;
    material.glass = glass;
    material.glassPattern = asset::GlassPattern::Plain;
    data.materials.push_back(material);
    data.bounds = AABB{Vec3(-4.0f, -4.0f, 0.0f), Vec3(4.0f, 4.0f, 0.0f)};
    return data;
}

/// Sum of the red bytes of rows y0..y1 (bottom row first).
u64 red(const std::vector<u8>& pixels, i32 y0, i32 y1)
{
    u64 sum = 0;
    for (i32 y = y0; y < y1; ++y)
    {
        for (i32 x = 0; x < kSize; ++x)
        {
            sum += pixels[static_cast<usize>((y * kSize + x) * 4)];
        }
    }
    return sum;
}

struct Scene
{
    GlFixture gl;
    ShaderLibrary library{*gl.device, fs::fromUtf8(G7_SHADER_DIR)};
    MeshRenderer renderer = require(MeshRenderer::create(*gl.device, library, 1.0f));
    ScreenHistory history = require(ScreenHistory::create(*gl.device, library));
    // The scene as the engine draws it (linear HDR), and an RGBA8 image to read.
    Texture sceneColor = require(gl.device->createTexture({kSize, kSize, Format::RGBA16F, 1}));
    Texture sceneDepth = require(gl.device->createTexture({kSize, kSize, Format::Depth32F, 1}));
    Framebuffer scene = require(gl.device->createFramebuffer({{&sceneColor}, &sceneDepth}));
    Texture color = require(gl.device->createTexture({kSize, kSize, Format::RGBA8, 1}));
    Texture depth = require(gl.device->createTexture({kSize, kSize, Format::Depth32F, 1}));
    Framebuffer target = require(gl.device->createFramebuffer({{&color}, &depth}));
    LightList none;
    /// Dark: what the glass shows comes from the panel.
    Environment environment{.sunIntensity = 0.0f,
                            .ambientSky = Vec3(0.0f),
                            .ambientGround = Vec3(0.0f),
                            .fogColor = Vec3(0.0f),
                            .fogStart = 1000.0f,
                            .fogDensity = 0.0f,
                            .skyZenith = Vec3(0.0f)};
    asset::MeshData panelData = quad(false, Vec3(5.0f, 1.0f, 1.0f));
    asset::MeshData glassData = quad(true);
    Mesh panel = require(Mesh::create(*gl.device, panelData));
    Mesh glass = require(Mesh::create(*gl.device, glassData));
    MaterialSet panelMaterials = require(MaterialSet::create(*gl.device, panelData, {}, renderer.defaults()));
    MaterialSet glassMaterials = require(MaterialSet::create(*gl.device, glassData, {}, renderer.defaults()));
    /// Upright, 6 m ahead, standing on the glass floor.
    Mat4 panelModel = glm::translate(Mat4(1.0f), Vec3(0.0f, 4.0f, -6.0f));
    /// A 16 x 16 m glass floor at y 0 facing up.
    Mat4 floorModel = glm::rotate(Mat4(1.0f), glm::radians(-90.0f), Vec3(1.0f, 0.0f, 0.0f)) *
                      glm::scale(Mat4(1.0f), Vec3(2.0f));
    Camera camera;

    Scene()
    {
        camera.aspect = 1.0f;
        camera.transform.position = Vec3(0.0f, 2.0f, 6.0f);
        camera.transform.rotation = glm::angleAxis(glm::radians(-15.0f), Vec3(1.0f, 0.0f, 0.0f));
    }

    void drawAll(const Framebuffer& fb, WindowReflections quality, bool withPanel)
    {
        gl.device->bindFramebuffer(&fb);
        gl.device->setViewport(0, 0, kSize, kSize);
        gl.device->clear(Vec4(0, 0, 0, 1), 0.0f);
        environment.windowReflections = quality;
        renderer.setLighting(*gl.device, environment, none);
        if (withPanel)
        {
            renderer.draw(*gl.device, panel, panelMaterials, panelModel, camera);
        }
        renderer.draw(*gl.device, glass, glassMaterials, floorModel, camera);
    }

    /// The previous frame: the scene (with or without the panel) into the history.
    void previousFrame(bool withPanel)
    {
        environment.ssr = false;
        renderer.setScreenHistory(nullptr, nullptr);
        drawAll(scene, WindowReflections::Medium, withPanel);
        REQUIRE(history.capture(*gl.device, sceneColor, sceneDepth, camera).ok());
    }

    std::vector<u8> frame(WindowReflections quality)
    {
        environment.ssr = quality == WindowReflections::High && history.valid();
        environment.ssrViewProjection = history.camera().viewProjection();
        environment.ssrCameraPosition = history.camera().transform.position;
        environment.ssrCameraForward = history.camera().transform.forward();
        renderer.setScreenHistory(&history.color(), &history.depth());
        drawAll(target, quality, true);
        return gl.device->readPixels(0, 0, kSize, kSize, &target);
    }
};
} // namespace

TEST_CASE("Glass SSR GPU: with high a panel on the screen shows in the glass below it, medium only the sky")
{
    Scene s;
    s.previousFrame(true);
    const auto medium = s.frame(WindowReflections::Medium);
    const auto high = s.frame(WindowReflections::High);
    // The glass floor fills the lower part of the image; the panel's mirror image lies there with high.
    const i32 lower = kSize / 3;
    MESSAGE("red in the lower third: high ", red(high, 0, lower), ", medium ", red(medium, 0, lower));
    CHECK(red(high, 0, lower) > red(medium, 0, lower) + 2000);
    CHECK(s.gl.device->debugErrorCount() == 0);
}

TEST_CASE("Glass SSR GPU: where the ray finds nothing in the previous frame the glass stays as with medium")
{
    Scene s;
    s.previousFrame(false); // the previous frame without the panel: the rays go into the empty sky
    CHECK(s.frame(WindowReflections::High) == s.frame(WindowReflections::Medium));
    CHECK(s.gl.device->debugErrorCount() == 0);
}
