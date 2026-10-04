// Multi-draw batches (label "gpu"): grouped by material values, the same picture as drawing every
// mesh on its own; translucent submeshes and meshes outside an arena are drawn singly.

#include "GlFixture.hpp"

#include <g7/render/Camera.hpp>
#include <g7/render/GeometryArena.hpp>
#include <g7/render/Lighting.hpp>
#include <g7/render/Material.hpp>
#include <g7/render/Mesh.hpp>
#include <g7/render/ShaderLibrary.hpp>
#include <g7/render/Shadows.hpp>

#include <vector>

using namespace g7;
using namespace g7::render;
using namespace g7::render::rhi;
using g7::test::GlFixture;
using g7::test::require;

namespace
{
/// A unit quad facing +Z with one material of the given colour and alpha mode.
asset::MeshData quad(const Vec4& colour, asset::AlphaMode mode = asset::AlphaMode::Opaque)
{
    asset::MeshData data;
    for (const Vec2 p : {Vec2(-0.5f, -0.5f), Vec2(0.5f, -0.5f), Vec2(0.5f, 0.5f), Vec2(-0.5f, 0.5f)})
    {
        asset::Vertex v{};
        v.position = Vec3(p, 0.0f);
        v.normal = Vec3(0.0f, 0.0f, 1.0f);
        data.vertices.push_back(v);
    }
    data.indices = {0, 1, 2, 0, 2, 3};
    data.submeshes = {{0, 6, 0}};
    asset::MaterialInfo material;
    material.baseColor = colour;
    material.alphaMode = mode;
    data.materials.push_back(material);
    data.bounds = AABB{Vec3(-0.5f, -0.5f, 0.0f), Vec3(0.5f, 0.5f, 0.0f)};
    return data;
}

struct Model
{
    Mesh mesh;
    MaterialSet materials;
};
} // namespace

TEST_CASE("Mesh batches: grouped by material values, same picture as single draws")
{
    GlFixture gl;
    ShaderLibrary library(*gl.device, fs::fromUtf8(G7_SHADER_DIR));
    MeshRenderer renderer = require(MeshRenderer::create(*gl.device, library, 1.0f));
    GeometryArena arena;

    // 8 x 8 quads in two colours (each quad its own model, like the half-timbered houses), one
    // translucent quad in front, one quad outside the arena.
    std::vector<Model> models;
    const Vec4 red(0.8f, 0.1f, 0.1f, 1.0f);
    const Vec4 blue(0.1f, 0.1f, 0.8f, 1.0f);
    for (int i = 0; i < 64; ++i)
    {
        const asset::MeshData data = quad(i % 3 == 0 ? blue : red);
        models.push_back({require(Mesh::create(*gl.device, arena, data)),
                          require(MaterialSet::create(*gl.device, data, {}, renderer.defaults()))});
    }
    const asset::MeshData glass = quad(Vec4(0.1f, 0.9f, 0.1f, 0.5f), asset::AlphaMode::Blend);
    models.push_back({require(Mesh::create(*gl.device, arena, glass)),
                      require(MaterialSet::create(*gl.device, glass, {}, renderer.defaults()))});
    const asset::MeshData own = quad(red);
    models.push_back({require(Mesh::create(*gl.device, own)),
                      require(MaterialSet::create(*gl.device, own, {}, renderer.defaults()))});

    std::vector<MeshDrawItem> items;
    for (usize i = 0; i < 64; ++i)
    {
        const Mat4 model = glm::translate(Mat4(1.0f), Vec3(f32(i % 8) - 3.5f, f32(i / 8) - 3.5f, 0.0f)) *
                           glm::scale(Mat4(1.0f), Vec3(0.9f));
        items.push_back(
            {&models[i].mesh, &models[i].materials, model, models[i].mesh.bounds().transformed(model)});
    }
    const Mat4 front =
        glm::translate(Mat4(1.0f), Vec3(0.0f, 0.0f, 1.0f)) * glm::scale(Mat4(1.0f), Vec3(3.0f));
    items.push_back(
        {&models[64].mesh, &models[64].materials, front, models[64].mesh.bounds().transformed(front)});
    const Mat4 corner = glm::translate(Mat4(1.0f), Vec3(3.5f, 3.5f, 0.5f));
    items.push_back(
        {&models[65].mesh, &models[65].materials, corner, models[65].mesh.bounds().transformed(corner)});

    Texture color = require(gl.device->createTexture({64, 64, Format::RGBA8, 1}));
    Texture depth = require(gl.device->createTexture({64, 64, Format::Depth32F, 1}));
    Framebuffer target = require(gl.device->createFramebuffer({{&color}, &depth}));
    Camera camera;
    camera.aspect = 1.0f;
    camera.transform.position = Vec3(0.0f, 0.0f, 9.0f);
    const auto begin = [&]
    {
        gl.device->bindFramebuffer(&target);
        gl.device->setViewport(0, 0, 64, 64);
        gl.device->clear(Vec4(0, 0, 0, 1), 0.0f);
        const Environment light{.sunIntensity = 0.0f, .ambientSky = Vec3(1.0f), .ambientGround = Vec3(1.0f)};
        static const LightList none;
        renderer.setLighting(*gl.device, light, none);
    };

    // Reference: every item on its own (opaque first, then the translucent one).
    begin();
    for (const MeshDrawItem& item : items)
    {
        if (item.mesh != &models[64].mesh)
        {
            renderer.draw(*gl.device, *item.mesh, *item.materials, item.model, camera);
        }
    }
    renderer.draw(*gl.device, models[64].mesh, models[64].materials, front, camera);
    const auto single = gl.device->readPixels(0, 0, 64, 64, &target);

    renderer.beginFrame();
    begin();
    gl.device->beginFrame(64, 64, Vec4(0.0f));
    gl.device->bindFramebuffer(&target);
    gl.device->setViewport(0, 0, 64, 64);
    gl.device->clear(Vec4(0, 0, 0, 1), 0.0f);
    renderer.drawBatched(*gl.device, items, camera);
    const auto batched = gl.device->readPixels(0, 0, 64, 64, &target);
    CHECK(batched == single);
    // Two opaque materials in one block: two groups for 64 quads; the glass and the quad outside the
    // arena one by one.
    CHECK(renderer.lastBatch().groups == 2);
    CHECK(renderer.lastBatch().batchedDraws == 64);
    CHECK(renderer.lastBatch().singleDraws == 2);
    CHECK(gl.device->stats().drawCalls == 4);

    // Several passes in one frame append behind each other (here: twice the same picture).
    renderer.drawBatched(*gl.device, items, camera);
    CHECK(gl.device->readPixels(0, 0, 64, 64, &target) == single);

    // Shadow pass: depth only, grouped too, no GL errors.
    ShadowSettings settings;
    settings.resolution = 256;
    ShadowMap shadows = require(ShadowMap::create(*gl.device, settings));
    const auto cascades = computeCascades(camera, Vec3(0.3f, 1.0f, 0.5f), settings);
    shadows.begin(*gl.device);
    shadows.beginCascade(*gl.device, 0);
    renderer.drawShadowBatched(*gl.device, items, cascades[0]);
    CHECK(renderer.lastBatch().groups == 1);      // opaque depth needs no material
    CHECK(renderer.lastBatch().singleDraws == 1); // the quad outside the arena
    CHECK(gl.device->debugErrorCount() == 0);
}

TEST_CASE("Mesh batches: buffers grow with the number of draws")
{
    GlFixture gl;
    ShaderLibrary library(*gl.device, fs::fromUtf8(G7_SHADER_DIR));
    MeshRenderer renderer = require(MeshRenderer::create(*gl.device, library, 1.0f));
    GeometryArena arena;
    const asset::MeshData data = quad(Vec4(1.0f));
    const Mesh mesh = require(Mesh::create(*gl.device, arena, data));
    const MaterialSet materials = require(MaterialSet::create(*gl.device, data, {}, renderer.defaults()));
    std::vector<MeshDrawItem> items(10000, {&mesh, &materials, Mat4(1.0f), mesh.bounds()});
    Camera camera;
    renderer.beginFrame();
    renderer.drawBatched(*gl.device, items, camera);
    CHECK(renderer.lastBatch().batchedDraws == 10000);
    renderer.drawBatched(*gl.device, items, camera); // a second pass grows the buffers mid-frame
    CHECK(renderer.lastBatch().batchedDraws == 10000);
    CHECK(gl.device->debugErrorCount() == 0);
}

TEST_CASE("Mesh batches: a translucent quad on a wall is blended, flush or a little in front")
{
    // welt's finding (2026-10-04): translucent quads on house walls were not seen. The batched pass draws
    // them after the opaque groups and blends them, also when they lie flush on the wall (same depth passes).
    GlFixture gl;
    ShaderLibrary library(*gl.device, fs::fromUtf8(G7_SHADER_DIR));
    MeshRenderer renderer = require(MeshRenderer::create(*gl.device, library, 1.0f));
    GeometryArena arena;
    const asset::MeshData wallData = quad(Vec4(0.8f, 0.1f, 0.1f, 1.0f));
    const asset::MeshData glassData = quad(Vec4(0.1f, 0.9f, 0.1f, 0.5f), asset::AlphaMode::Blend);
    const Model wall{require(Mesh::create(*gl.device, arena, wallData)),
                     require(MaterialSet::create(*gl.device, wallData, {}, renderer.defaults()))};
    const Model glass{require(Mesh::create(*gl.device, arena, glassData)),
                      require(MaterialSet::create(*gl.device, glassData, {}, renderer.defaults()))};
    Texture color = require(gl.device->createTexture({32, 32, Format::RGBA8, 1}));
    Texture depth = require(gl.device->createTexture({32, 32, Format::Depth32F, 1}));
    Framebuffer target = require(gl.device->createFramebuffer({{&color}, &depth}));
    Camera camera;
    camera.aspect = 1.0f;
    camera.transform.position = Vec3(0.0f, 0.0f, 12.0f); // a house wall seen from the street
    const auto centreGreen = [&](f32 offset)
    {
        gl.device->bindFramebuffer(&target);
        gl.device->setViewport(0, 0, 32, 32);
        gl.device->clear(Vec4(0, 0, 0, 1), 0.0f);
        const Environment light{.sunIntensity = 0.0f, .ambientSky = Vec3(1.0f), .ambientGround = Vec3(1.0f)};
        static const LightList none;
        renderer.setLighting(*gl.device, light, none);
        renderer.beginFrame();
        const Mat4 big = glm::scale(Mat4(1.0f), Vec3(4.0f));
        const Mat4 onTop = glm::translate(Mat4(1.0f), Vec3(0.0f, 0.0f, offset)) * big;
        const MeshDrawItem items[] = {
            {&wall.mesh, &wall.materials, big, wall.mesh.bounds().transformed(big)},
            {&glass.mesh, &glass.materials, onTop, glass.mesh.bounds().transformed(onTop)}};
        renderer.drawBatched(*gl.device, items, camera);
        const auto p = gl.device->readPixels(16, 16, 1, 1, &target);
        return static_cast<int>(p[1]);
    };
    CHECK(centreGreen(0.002f) > 100); // 2 mm in front: blended over the wall
    CHECK(centreGreen(0.0f) > 100);   // flush on it too
    CHECK(gl.device->debugErrorCount() == 0);
}
