// Sun shadows on a real driver (label "gpu"): ground plate + box, pixels probed at world points.

#include "GlFixture.hpp"

#include <g7/asset/Procedural.hpp>
#include <g7/render/Camera.hpp>
#include <g7/render/Lighting.hpp>
#include <g7/render/Material.hpp>
#include <g7/render/Mesh.hpp>
#include <g7/render/ShaderLibrary.hpp>
#include <g7/render/Shadows.hpp>

#if defined(_MSC_VER)
#pragma warning(push, 0)
#endif
#include <stb_image_write.h> // implementation compiled in test_material.cpp
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

#include <glm/gtc/matrix_transform.hpp>

#include <array>
#include <cmath>

using namespace g7;
using namespace g7::render;
using namespace g7::render::rhi;
using g7::test::GlFixture;
using g7::test::require;

namespace
{
constexpr u32 kSize = 64;

struct Model
{
    Mesh mesh;
    MaterialSet materials;
    Mat4 transform{1.0f};
};

void appendBytes(void* context, void* data, int size)
{
    auto* out = static_cast<std::vector<u8>*>(context);
    out->insert(out->end(), static_cast<const u8*>(data), static_cast<const u8*>(data) + size);
}

struct Scene
{
    GlFixture gl;
    ShaderLibrary library{*gl.device, fs::fromUtf8(G7_SHADER_DIR)};
    ShadowSettings settings;
    MeshRenderer renderer;
    ShadowMap shadowMap;
    Texture color;
    Texture depth;
    Framebuffer target;
    Camera camera;
    Environment environment;
    LightList noLights;

    explicit Scene(ShadowSettings shadowSettings = {}) : settings(shadowSettings)
    {
        settings.resolution = 1024;
        renderer = require(MeshRenderer::create(*gl.device, library, 1.0f, settings));
        shadowMap = require(ShadowMap::create(*gl.device, settings));
        color = require(gl.device->createTexture({kSize, kSize, Format::RGBA8, 1}));
        depth = require(gl.device->createTexture({kSize, kSize, Format::Depth32F, 1}));
        target = require(gl.device->createFramebuffer({{&color}, &depth}));
        camera.aspect = 1.0f;
        environment = Environment{.sunDirection = Vec3(1, 1, 0),
                                  .sunColor = Vec3(1.0f),
                                  .sunIntensity = 1.0f,
                                  .ambientSky = Vec3(0.1f),
                                  .ambientGround = Vec3(0.1f)};
    }

    Model model(const asset::MeshData& data, const Vec3& position)
    {
        return {require(Mesh::create(*gl.device, data)), require(MaterialSet::create(*gl.device, data, {})),
                glm::translate(Mat4(1.0f), position)};
    }

    /// Renders ground + casters with or without shadows.
    void render(const Model& ground, std::span<const Model> casters, bool shadows = true)
    {
        const auto cascades = computeCascades(camera, environment.sunDirection, settings);
        ShadowFrame frame{&shadowMap, cascades, &camera, false};
        if (shadows)
        {
            shadowMap.begin(*gl.device);
            for (u32 i = 0; i < cascades.size(); ++i)
            {
                shadowMap.beginCascade(*gl.device, i);
                for (const Model& caster : casters)
                {
                    renderer.drawShadow(*gl.device, caster.mesh, caster.materials, caster.transform,
                                        cascades[i]);
                }
            }
        }
        gl.device->bindFramebuffer(&target);
        gl.device->setViewport(0, 0, kSize, kSize);
        gl.device->clear(Vec4(0, 0, 0, 1), 0.0f);
        renderer.setLighting(*gl.device, environment, noLights, shadows ? &frame : nullptr);
        renderer.draw(*gl.device, ground.mesh, ground.materials, ground.transform, camera);
        for (const Model& caster : casters)
        {
            renderer.draw(*gl.device, caster.mesh, caster.materials, caster.transform, camera);
        }
    }

    /// Brightness of the pixel showing world point `p`.
    int at(const Vec3& p)
    {
        const Vec4 clip = camera.viewProjection() * Vec4(p, 1.0f);
        const Vec2 ndc = Vec2(clip) / clip.w;
        const auto x = static_cast<i32>((ndc.x * 0.5f + 0.5f) * kSize);
        const auto y = static_cast<i32>((ndc.y * 0.5f + 0.5f) * kSize);
        REQUIRE(x >= 0);
        REQUIRE(x < static_cast<i32>(kSize));
        REQUIRE(y >= 0);
        REQUIRE(y < static_cast<i32>(kSize));
        const auto pixel = gl.device->readPixels(x, y, 1, 1, &target);
        return int(pixel[0]) + pixel[1] + pixel[2];
    }

    /// Camera high above the origin looking straight down.
    void lookDown(f32 height)
    {
        camera.transform.position = Vec3(0.0f, height, 0.0f);
        camera.transform.rotation = lookRotation(Vec3(0, -1, 0), Vec3(0, 0, -1));
    }
};
} // namespace

TEST_CASE("Shadows: a box casts a shadow onto the ground")
{
    Scene scene;
    scene.lookDown(12.0f);
    const Model ground = scene.model(asset::makePlane(30.0f), Vec3(0.0f));
    const std::array<Model, 1> box{scene.model(asset::makeBox(Vec3(1.0f)), Vec3(0, 2, 0))};
    // Sun from (1,1,0): the shadow of the box (y 1..3) falls 1..3 m towards -X.
    scene.render(ground, box);
    const int shadowed = scene.at(Vec3(-2.0f, 0, 0));
    const int lit = scene.at(Vec3(4.0f, 0, 0));
    CHECK(shadowed < lit / 2);

    // No acne: an unshadowed spot is as bright as without shadows at all.
    scene.render(ground, box, false);
    CHECK(std::abs(scene.at(Vec3(4.0f, 0, 0)) - lit) <= 3);
    CHECK(scene.gl.device->debugErrorCount() == 0);
}

TEST_CASE("Shadows: alpha-tested casters let light through their holes")
{
    Scene scene;
    scene.lookDown(12.0f);
    const Model ground = scene.model(asset::makePlane(30.0f), Vec3(0.0f));
    asset::MeshData leaves = asset::makeBox(Vec3(1.0f));
    leaves.materials[0].alphaMode = asset::AlphaMode::Mask;
    leaves.materials[0].baseColorImage = 0;
    asset::ImageSource transparent;
    const u8 pixel[4] = {255, 255, 255, 0};
    REQUIRE(stbi_write_png_to_func(appendBytes, &transparent.encoded, 1, 1, 4, pixel, 4) != 0);
    leaves.images = {transparent};
    const std::array<Model, 1> casters{scene.model(leaves, Vec3(0, 2, 0))};
    scene.render(ground, casters);
    CHECK(std::abs(scene.at(Vec3(-2.0f, 0, 0)) - scene.at(Vec3(4.0f, 0, 0))) <= 3); // no shadow
    CHECK(scene.gl.device->debugErrorCount() == 0);
}

TEST_CASE("Shadows: nothing beyond the shadow distance")
{
    ShadowSettings near;
    near.distance = 5.0f; // the ground is 12 m below the camera
    Scene scene(near);
    scene.lookDown(12.0f);
    const Model ground = scene.model(asset::makePlane(30.0f), Vec3(0.0f));
    const std::array<Model, 1> box{scene.model(asset::makeBox(Vec3(1.0f)), Vec3(0, 2, 0))};
    scene.render(ground, box);
    CHECK(std::abs(scene.at(Vec3(-2.0f, 0, 0)) - scene.at(Vec3(4.0f, 0, 0))) <= 3);
}

TEST_CASE("Shadows: casters behind the camera still shade the view")
{
    Scene scene;
    // Camera low above the ground looking along -Z; the box hangs behind it (z = +1).
    scene.camera.transform.position = Vec3(0.0f, 1.5f, 0.0f);
    scene.camera.transform.rotation = quatFromEuler(toRadians(-30.0f), 0.0f, 0.0f);
    scene.environment.sunDirection = Vec3(0, 1, 1); // light travels towards -Z, -Y
    const Model ground = scene.model(asset::makePlane(40.0f), Vec3(0.0f));
    const std::array<Model, 1> box{scene.model(asset::makeBox(Vec3(0.75f)), Vec3(0, 3, 1))};
    scene.render(ground, box);
    // Shadow centre: (0, 0, 1 - 3) = (0, 0, -2), 1.5 m wide; compare with a spot beside it (still in view).
    CHECK(scene.at(Vec3(0, 0, -2)) < scene.at(Vec3(1.4f, 0, -2)) / 2);
    CHECK(scene.gl.device->debugErrorCount() == 0);
}
