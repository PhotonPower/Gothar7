// Forward lighting on a real driver (label "gpu"): sun, hemisphere ambient, point lights.

#include "GlFixture.hpp"

#include <g7/render/Camera.hpp>
#include <g7/render/Lighting.hpp>
#include <g7/render/Material.hpp>
#include <g7/render/Mesh.hpp>
#include <g7/render/ShaderLibrary.hpp>

#include <array>

using namespace g7;
using namespace g7::render;
using namespace g7::render::rhi;
using g7::test::GlFixture;
using g7::test::require;

namespace
{
/// White 2x2 m quad in the XY plane (facing the camera on +Z); `normal` is the shading normal.
asset::MeshData quad(const Vec3& normal = Vec3(0, 0, 1))
{
    asset::MeshData data;
    data.vertices = {{Vec3(-1, -1, 0), normal, Vec2(0, 1), Vec4(0)},
                     {Vec3(1, -1, 0), normal, Vec2(1, 1), Vec4(0)},
                     {Vec3(1, 1, 0), normal, Vec2(1, 0), Vec4(0)},
                     {Vec3(-1, 1, 0), normal, Vec2(0, 0), Vec4(0)}};
    data.indices = {0, 1, 2, 0, 2, 3};
    data.submeshes = {{0, 6, 0}};
    data.materials = {asset::MaterialInfo{}};
    data.bounds = AABB{Vec3(-1, -1, 0), Vec3(1, 1, 0)};
    return data;
}

Environment dark()
{
    return Environment{.sunIntensity = 0.0f, .ambientSky = Vec3(0.0f), .ambientGround = Vec3(0.0f)};
}

struct Scene
{
    GlFixture gl;
    ShaderLibrary library{*gl.device, fs::fromUtf8(G7_SHADER_DIR)};
    MeshRenderer renderer;
    Texture color;
    Texture depth;
    Framebuffer target;

    Scene()
    {
        renderer = require(MeshRenderer::create(*gl.device, library, 1.0f));
        color = require(gl.device->createTexture({16, 16, Format::RGBA8, 1}));
        depth = require(gl.device->createTexture({16, 16, Format::Depth32F, 1}));
        target = require(gl.device->createFramebuffer({{&color}, &depth}));
    }

    std::array<u8, 4> centre(const Environment& environment, const LightList& lights,
                             const asset::MeshData& data = quad())
    {
        Mesh mesh = require(Mesh::create(*gl.device, data));
        MaterialSet materials = require(MaterialSet::create(*gl.device, data, fs::Path(".")));
        Camera camera;
        camera.aspect = 1.0f;
        camera.transform.position = Vec3(0, 0, 2.5f);
        gl.device->bindFramebuffer(&target);
        gl.device->setViewport(0, 0, 16, 16);
        gl.device->clear(Vec4(0, 0, 0, 1), 0.0f);
        renderer.setLighting(*gl.device, environment, lights);
        renderer.draw(*gl.device, mesh, materials, Mat4(1.0f), camera);
        const auto p = gl.device->readPixels(8, 8, 1, 1, &target);
        REQUIRE(p.size() == 4);
        return {p[0], p[1], p[2], p[3]};
    }
};

int brightness(const std::array<u8, 4>& p)
{
    return int(p[0]) + p[1] + p[2];
}
} // namespace

TEST_CASE("Lighting: the sun lights surfaces facing it")
{
    Scene scene;
    const LightList none;
    Environment sun = dark();
    sun.sunIntensity = 1.0f;
    sun.sunDirection = Vec3(0, 0, 1); // straight onto the quad
    CHECK(scene.centre(sun, none) == std::array<u8, 4>{255, 255, 255, 255});
    sun.sunDirection = Vec3(0, 0, -1); // behind it
    CHECK(brightness(scene.centre(sun, none)) == 0);
    sun.sunDirection = Vec3(0, 1, 1); // 45°: cos = 0.707 -> linear output ~ 180
    const auto slanted = scene.centre(sun, none);
    CHECK(slanted[0] > 170);
    CHECK(slanted[0] < 190);
    CHECK(scene.gl.device->debugErrorCount() == 0);
}

TEST_CASE("Lighting: hemisphere ambient by normal direction")
{
    Scene scene;
    const LightList none;
    Environment ambient = dark();
    ambient.ambientSky = Vec3(1, 0, 0);
    ambient.ambientGround = Vec3(0, 0, 1);
    const auto up = scene.centre(ambient, none, quad(Vec3(0, 1, 0)));
    CHECK(up[0] == 255);
    CHECK(up[2] == 0);
    const auto down = scene.centre(ambient, none, quad(Vec3(0, -1, 0)));
    CHECK(down[0] == 0);
    CHECK(down[2] == 255);
    CHECK(scene.gl.device->debugErrorCount() == 0);
}

TEST_CASE("Lighting: point lights reach only within their radius and add up")
{
    Scene scene;
    LightList lights;
    lights.add({Vec3(0, 0, 1), 3.0f, Vec3(1.0f), 1.0f});
    const auto one = scene.centre(dark(), lights);
    CHECK(brightness(one) > 300);
    CHECK(one[0] == one[1]);

    LightList two = lights;
    two.add({Vec3(0.5f, 0, 1), 3.0f, Vec3(1.0f), 1.0f});
    CHECK(brightness(scene.centre(dark(), two)) > brightness(one));

    LightList far;
    far.add({Vec3(0, 0, 5), 3.0f, Vec3(1.0f), 100.0f}); // 5 m away, radius 3: never reaches
    CHECK(brightness(scene.centre(dark(), far)) == 0);

    LightList coloured;
    coloured.add({Vec3(0, 0, 1), 3.0f, Vec3(1, 0.5f, 0), 1.0f}); // warm torch
    const auto warm = scene.centre(dark(), coloured);
    CHECK(warm[0] > warm[1]);
    CHECK(warm[2] == 0);
    CHECK(scene.gl.device->debugErrorCount() == 0);
}

TEST_CASE("Lighting: lights behind the surface do not light it")
{
    Scene scene;
    LightList behind;
    behind.add({Vec3(0, 0, -1), 3.0f, Vec3(1.0f), 5.0f});
    CHECK(brightness(scene.centre(dark(), behind)) == 0);
}
