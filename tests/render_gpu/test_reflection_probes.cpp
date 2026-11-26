// Reflection probes on the GPU (M17 windows F2, label "gpu"): a bright panel drawn into a probe's faces lands
// on the face looking at it; window glass with window reflections medium shows it - right way up and on the
// right side -, a probe too far away does not count (the sky alone, as with low).

#include "GlFixture.hpp"

#include <g7/render/Camera.hpp>
#include <g7/render/Glass.hpp>
#include <g7/render/Lighting.hpp>
#include <g7/render/Material.hpp>
#include <g7/render/Mesh.hpp>
#include <g7/render/ReflectionProbes.hpp>
#include <g7/render/ShaderLibrary.hpp>

#include <optional>
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

/// Sum of the red bytes over a region of an RGBA8 image (kSize wide).
u64 red(const std::vector<u8>& pixels, i32 x0, i32 y0, i32 x1, i32 y1)
{
    u64 sum = 0;
    for (i32 y = y0; y < y1; ++y)
    {
        for (i32 x = x0; x < x1; ++x)
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
    ReflectionProbes probes = require(ReflectionProbes::create(*gl.device, {.faceSize = 64, .slots = 2}));
    Texture color = require(gl.device->createTexture({kSize, kSize, Format::RGBA8, 1}));
    Texture depth = require(gl.device->createTexture({kSize, kSize, Format::Depth32F, 1}));
    Framebuffer target = require(gl.device->createFramebuffer({{&color}, &depth}));
    LightList none;
    /// Dark: no sun, no ambient, a black sky - what the glass shows comes from the probe.
    Environment environment{.sunIntensity = 0.0f,
                            .ambientSky = Vec3(0.0f),
                            .ambientGround = Vec3(0.0f),
                            .fogColor = Vec3(0.0f),
                            .fogStart = 1000.0f,
                            .fogDensity = 0.0f,
                            .skyZenith = Vec3(0.0f)};
    asset::MeshData panelData = quad(false, Vec3(5.0f, 1.0f, 1.0f));
    Mesh panel = require(Mesh::create(*gl.device, panelData));
    MaterialSet panelMaterials = require(MaterialSet::create(*gl.device, panelData, {}, renderer.defaults()));
    /// The panel 4 x 4 m, its centre 2 m right and up, 10 m along +Z, facing back (-Z).
    Mat4 panelModel = glm::translate(Mat4(1.0f), Vec3(2.0f, 2.0f, 10.0f)) *
                      glm::rotate(Mat4(1.0f), glm::pi<f32>(), Vec3(0.0f, 1.0f, 0.0f)) *
                      glm::scale(Mat4(1.0f), Vec3(0.5f));

    /// Draws all faces of a probe at `position` (box out to the panel's plane) with the panel in them.
    void fill(const Vec3& position)
    {
        probes.schedule().setProbes(
            {{position, AABB{Vec3(-50.0f, -50.0f, -50.0f) + Vec3(position.x, position.y, 0.0f),
                             Vec3(50.0f + position.x, 50.0f + position.y, 10.0f)}}});
        std::vector<ProbeFace> faces;
        probes.schedule().update(position, 100, faces);
        REQUIRE(faces.size() == kProbeFaces);
        renderer.setReflectionProbes(nullptr);
        environment.windowReflections = WindowReflections::Low;
        environment.probes = {};
        renderer.setLighting(*gl.device, environment, none);
        for (const ProbeFace& face : faces)
        {
            probes.beginFace(*gl.device, face);
            gl.device->clear(Vec4(0.0f, 0.0f, 0.0f, 1.0f), std::nullopt); // the sky: black (scissored)
            renderer.draw(*gl.device, panel, panelMaterials, panelModel,
                          probeFaceCamera(position, face.face, 60.0f));
            probes.endFace(*gl.device, face);
        }
        probes.schedule().update(position, 0, faces);
        REQUIRE(probes.schedule().shaderProbes().size() == 1);
    }

    /// The glass pane (8 x 8 m at z 0 facing +Z) from 3 m in front, with the probes or without.
    std::vector<u8> glass(WindowReflections quality)
    {
        const asset::MeshData data = quad(true);
        const Mesh mesh = require(Mesh::create(*gl.device, data));
        const MaterialSet materials = require(MaterialSet::create(*gl.device, data, {}, renderer.defaults()));
        Camera camera;
        camera.aspect = 1.0f;
        camera.transform.position = Vec3(0.0f, 0.0f, 3.0f);
        gl.device->bindFramebuffer(&target);
        gl.device->setViewport(0, 0, kSize, kSize);
        gl.device->clear(Vec4(0, 0, 0, 1), 0.0f);
        environment.windowReflections = quality;
        environment.probes = probes.schedule().shaderProbes();
        environment.probeParams = probes.shaderParams();
        renderer.setReflectionProbes(&probes.atlas());
        renderer.setLighting(*gl.device, environment, none);
        renderer.draw(*gl.device, mesh, materials, Mat4(1.0f), camera);
        return gl.device->readPixels(0, 0, kSize, kSize, &target);
    }
};
} // namespace

TEST_CASE("Probes GPU: the panel lands on the face looking at it, the others stay dark")
{
    Scene s;
    s.fill(Vec3(0.0f, 0.0f, 4.0f));
    const auto tileRed = [&](u32 face)
    {
        const PixelRect tile = s.probes.schedule().tile(0, face);
        const auto pixels = s.gl.device->readPixels(tile.x, tile.y, static_cast<i32>(tile.width),
                                                    static_cast<i32>(tile.height), &s.probes.framebuffer());
        u64 sum = 0;
        for (usize i = 0; i < pixels.size(); i += 4)
        {
            sum += pixels[i];
        }
        return sum;
    };
    CHECK(tileRed(4) > 64 * 64 * 20); // +Z sees the panel
    for (const u32 face : {0u, 1u, 2u, 3u, 5u})
    {
        CHECK(tileRed(face) < tileRed(4) / 10);
    }
    CHECK(s.gl.device->debugErrorCount() == 0);
}

TEST_CASE("Probes GPU: glass mirrors the probe - the panel up and right as it stands -, a far probe not")
{
    Scene s;
    const u64 sky = red(s.glass(WindowReflections::Low), 0, 0, kSize, kSize);
    s.fill(Vec3(0.0f, 0.0f, 4.0f));
    const auto mirrored = s.glass(WindowReflections::Medium);
    const u64 all = red(mirrored, 0, 0, kSize, kSize);
    CHECK(all > sky + 2000);
    // The panel is up and to the right (+X, +Y): its mirror image too (the camera looks along -Z, x right).
    const i32 h = kSize / 2;
    CHECK(red(mirrored, h, h, kSize, kSize) > 4 * red(mirrored, 0, 0, h, h));
    CHECK(red(mirrored, h, h, kSize, kSize) > 4 * red(mirrored, 0, h, h, kSize));
    CHECK(red(mirrored, h, h, kSize, kSize) > 4 * red(mirrored, h, 0, kSize, h));
    // With low the probes are not read.
    CHECK(red(s.glass(WindowReflections::Low), 0, 0, kSize, kSize) == sky);

    // The same probe 80 m away: beyond the fade, the glass shows the sky alone.
    s.fill(Vec3(0.0f, 0.0f, 84.0f));
    CHECK(red(s.glass(WindowReflections::Medium), 0, 0, kSize, kSize) == sky);
    CHECK(s.gl.device->debugErrorCount() == 0);
}
