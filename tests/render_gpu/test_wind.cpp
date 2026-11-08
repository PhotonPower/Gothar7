// Wind on the GPU (M17 weather part W, label "gpu"): the tip of a marked test model moves with the wind, its
// foot does not; without wind, with the option off and for unmarked materials everything stands still; the
// batched path and the depth-only (shadow, prepass) path bend the same.

#include "GlFixture.hpp"

#include <g7/render/Camera.hpp>
#include <g7/render/GeometryArena.hpp>
#include <g7/render/Lighting.hpp>
#include <g7/render/Material.hpp>
#include <g7/render/Mesh.hpp>
#include <g7/render/ShaderLibrary.hpp>
#include <g7/render/Shadows.hpp>
#include <g7/render/Wind.hpp>

#include <vector>

using namespace g7;
using namespace g7::render;
using namespace g7::render::rhi;
using g7::test::GlFixture;
using g7::test::require;

namespace
{
constexpr i32 kSize = 128;

/// A pole 0.4 m wide and 10 m high facing +Z, foot at y = 0, white.
asset::MeshData pole(asset::WindKind wind)
{
    asset::MeshData data;
    for (const Vec2 p : {Vec2(-0.2f, 0.0f), Vec2(0.2f, 0.0f), Vec2(0.2f, 10.0f), Vec2(-0.2f, 10.0f)})
    {
        asset::Vertex v{};
        v.position = Vec3(p, 0.0f);
        v.normal = Vec3(0.0f, 0.0f, 1.0f);
        data.vertices.push_back(v);
    }
    data.indices = {0, 1, 2, 0, 2, 3};
    data.submeshes = {{0, 6, 0}};
    asset::MaterialInfo material;
    material.wind = wind;
    data.materials.push_back(material);
    data.bounds = AABB{Vec3(-0.2f, 0.0f, 0.0f), Vec3(0.2f, 10.0f, 0.0f)};
    return data;
}

/// The mean column of the lit pixels in an image row (-1: none lit).
f32 litColumn(const std::vector<u8>& pixels, i32 row)
{
    f32 sum = 0.0f;
    f32 count = 0.0f;
    for (i32 x = 0; x < kSize; ++x)
    {
        if (pixels[static_cast<usize>((row * kSize + x) * 4)] > 128)
        {
            sum += static_cast<f32>(x);
            count += 1.0f;
        }
    }
    return count > 0.0f ? sum / count : -1.0f;
}

struct Scene
{
    GlFixture gl;
    ShaderLibrary library{*gl.device, fs::fromUtf8(G7_SHADER_DIR)};
    MeshRenderer renderer = require(MeshRenderer::create(*gl.device, library, 1.0f));
    GeometryArena arena;
    Texture color = require(gl.device->createTexture({kSize, kSize, Format::RGBA8, 1}));
    Texture depth = require(gl.device->createTexture({kSize, kSize, Format::Depth32F, 1}));
    Framebuffer target = require(gl.device->createFramebuffer({{&color}, &depth}));
    Camera camera;
    LightList none;

    Scene()
    {
        camera.aspect = 1.0f;
        camera.transform.position = Vec3(0.0f, 5.0f, 16.0f);
    }

    void begin()
    {
        gl.device->bindFramebuffer(&target);
        gl.device->setViewport(0, 0, kSize, kSize);
        gl.device->clear(Vec4(0, 0, 0, 1), 0.0f);
        const Environment light{.sunIntensity = 0.0f, .ambientSky = Vec3(1.0f), .ambientGround = Vec3(1.0f)};
        renderer.setLighting(*gl.device, light, none);
    }

    std::vector<u8> draw(const Mesh& mesh, const MaterialSet& materials, const Mat4& model)
    {
        begin();
        renderer.draw(*gl.device, mesh, materials, model, camera);
        return gl.device->readPixels(0, 0, kSize, kSize, &target);
    }
};

WindFrame fullWind()
{
    // time 0.7: the grass sway term (0.7 + 0.3 sin) is near its top for the pole's place.
    return WindFrame{.direction = Vec2(1.0f, 0.0f), .strength = 1.0f, .gusts = 0.0f, .time = 0.7f};
}
} // namespace

TEST_CASE("Wind GPU: the tip moves downwind, the foot stays; still without wind or marks")
{
    Scene s;
    const asset::MeshData grassData = pole(asset::WindKind::Grass);
    const asset::MeshData wallData = pole(asset::WindKind::None);
    const Mesh grass = require(Mesh::create(*s.gl.device, grassData));
    const MaterialSet grassMaterials =
        require(MaterialSet::create(*s.gl.device, grassData, {}, s.renderer.defaults()));
    const Mesh wall = require(Mesh::create(*s.gl.device, wallData));
    const MaterialSet wallMaterials =
        require(MaterialSet::create(*s.gl.device, wallData, {}, s.renderer.defaults()));
    REQUIRE(grassMaterials[0].wind.kind == asset::WindKind::Grass);
    CHECK(grassMaterials[0].wind.height == doctest::Approx(10.0f));
    const Mat4 model(1.0f);

    // Rows (readPixels: row 0 at the bottom): near the foot and near the tip.
    s.renderer.setWind(WindFrame{});
    const auto still = s.draw(grass, grassMaterials, model);
    i32 footRow = -1;
    i32 tipRow = -1;
    for (i32 y = 0; y < kSize; ++y)
    {
        if (litColumn(still, y) >= 0.0f)
        {
            footRow = footRow < 0 ? y + 2 : footRow;
            tipRow = y - 2;
        }
    }
    REQUIRE(footRow >= 0);
    REQUIRE(tipRow > footRow + 40);

    s.renderer.setWind(fullWind());
    const auto windy = s.draw(grass, grassMaterials, model);
    CHECK(windy != still);
    // The tip (25 % of 10 m at full wind, about 1.8 to 2.5 m) moved right, the foot did not.
    const f32 pixelsPerMetre = static_cast<f32>(tipRow - footRow) / 9.6f;
    const f32 expected = glm::length(
        windBent(fullWind(), grassMaterials[0].wind, model, Vec3(0.0f, 9.8f, 0.0f)) - Vec3(0.0f, 9.8f, 0.0f));
    const f32 shift = litColumn(windy, tipRow) - litColumn(still, tipRow);
    CHECK(shift > 0.5f * expected * pixelsPerMetre);
    CHECK(litColumn(windy, footRow) == doctest::Approx(litColumn(still, footRow)).epsilon(0.02));

    // Unmarked: still in the same wind. Strength 0 (option off): still.
    CHECK(s.draw(wall, wallMaterials, model) == still);
    WindFrame calm = fullWind();
    calm.strength = 0.0f;
    s.renderer.setWind(calm);
    CHECK(s.draw(grass, grassMaterials, model) == still);
}

TEST_CASE("Wind GPU: batched draws and the depth-only pass bend like single draws")
{
    Scene s;
    const asset::MeshData data = pole(asset::WindKind::Grass);
    const Mesh mesh = require(Mesh::create(*s.gl.device, s.arena, data));
    const MaterialSet materials = require(MaterialSet::create(*s.gl.device, data, {}, s.renderer.defaults()));
    const Mat4 model = glm::translate(Mat4(1.0f), Vec3(-2.0f, 0.0f, 0.0f));
    const std::vector<MeshDrawItem> items{{&mesh, &materials, model, mesh.bounds().transformed(model)}};

    s.renderer.setWind(fullWind());
    const auto single = s.draw(mesh, materials, model);
    s.renderer.beginFrame();
    s.begin();
    s.renderer.drawBatched(*s.gl.device, items, s.camera);
    CHECK(s.gl.device->readPixels(0, 0, kSize, kSize, &s.target) == single);
    CHECK(s.renderer.lastBatch().batchedDraws == 1);

    // Depth only (shadow cascades, the depth prepass): the bent pole's depth, then a still wall just behind
    // it - the wall shows everywhere except where the depth pass put the pole.
    asset::MeshData wallData = pole(asset::WindKind::None);
    for (asset::Vertex& v : wallData.vertices)
    {
        v.position = Vec3(v.position.x * 100.0f, v.position.y * 3.0f - 10.0f, -0.05f);
    }
    wallData.bounds = AABB{Vec3(-20.0f, -10.0f, -0.05f), Vec3(20.0f, 20.0f, -0.05f)};
    const Mesh wall = require(Mesh::create(*s.gl.device, wallData));
    const MaterialSet wallMaterials =
        require(MaterialSet::create(*s.gl.device, wallData, {}, s.renderer.defaults()));
    const auto holeOf = [&](const WindFrame& wind)
    {
        s.renderer.setWind(wind);
        s.renderer.beginFrame();
        s.begin();
        s.renderer.drawDepthBatched(*s.gl.device, items, s.camera);
        s.renderer.draw(*s.gl.device, wall, wallMaterials, Mat4(1.0f), s.camera);
        return s.gl.device->readPixels(0, 0, kSize, kSize, &s.target);
    };
    const auto bentHole = holeOf(fullWind());
    const auto stillHole = holeOf(WindFrame{});
    CHECK(bentHole != stillHole);
    usize mismatched = 0;
    usize poleArea = 0;
    for (usize i = 0; i < single.size(); i += 4)
    {
        const bool drawn = single[i] > 128;
        const bool hole = bentHole[i] < 128;
        mismatched += drawn != hole ? 1 : 0;
        poleArea += drawn ? 1 : 0;
    }
    CHECK(poleArea > 40);
    CHECK_MESSAGE(mismatched < poleArea / 5, mismatched); // at most a few edge pixels
}
