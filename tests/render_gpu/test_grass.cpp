// Grass on the GPU (M17 part G1): tufts show on the meadow and not on the cobbles beside it, nothing is drawn
// with the quality off, the wind moves the blades.

#include "GlFixture.hpp"

#include <g7/render/Camera.hpp>
#include <g7/render/Grass.hpp>
#include <g7/render/Lighting.hpp>
#include <g7/render/Material.hpp>
#include <g7/render/ShaderLibrary.hpp>

#include <doctest/doctest.h>

#include <span>
#include <vector>

using namespace g7;
using namespace g7::render;
using namespace g7::render::rhi;
using g7::test::GlFixture;
using g7::test::require;

namespace
{
/// Flat ground at y = 0: meadow for x < 0, cobbles for x >= 0.
struct HalfMeadow final : GrassGround
{
    bool sample(const Vec2& xz, GrassGroundSample& out) const override
    {
        out.height = 0.0f;
        out.weights[0] = xz.x < 0.0f ? 1.0f : 0.0f;
        out.weights[1] = 1.0f - out.weights[0];
        return true;
    }
};

constexpr u32 kSize = 64;

struct GrassScene
{
    GlFixture gl;
    ShaderLibrary library{*gl.device, fs::fromUtf8(G7_SHADER_DIR)};
    MeshRenderer meshes;
    GrassRenderer grass;
    Texture color;
    Texture depth;
    Framebuffer target;
    GrassRing ring;
    std::vector<GrassLayer> layers{builtinGrassLayer("Wiese"), builtinGrassLayer("Kopfstein")};
    Camera camera;

    GrassScene()
    {
        meshes = require(MeshRenderer::create(*gl.device, library, 1.0f));
        grass = require(GrassRenderer::create(*gl.device, library, ShadowSettings{}));
        color = require(gl.device->createTexture({kSize, kSize, Format::RGBA8, 1}));
        depth = require(gl.device->createTexture({kSize, kSize, Format::Depth32F, 1}));
        target = require(gl.device->createFramebuffer({{&color}, &depth}));
        // Looking down the z axis from 2 m up and 6 m back: the meadow on the left, the cobbles on the right.
        camera.transform.position = Vec3(0.0f, 2.0f, 6.0f);
        camera.transform.rotation = glm::angleAxis(glm::radians(-25.0f), Vec3(1.0f, 0.0f, 0.0f));
        camera.aspect = 1.0f;
    }

    void fill(GrassQuality quality)
    {
        const GrassSettings settings = grassSettings(quality);
        ring.reset(settings.radius, settings.tuftsPerSquareMetre);
        REQUIRE(grass.reserve(*gl.device, ring).ok());
        HalfMeadow ground;
        std::vector<u32> built;
        for (int i = 0; i < 100; ++i)
        {
            ring.update(camera.transform.position, ground, layers, built);
        }
        for (const u32 slot : built)
        {
            REQUIRE(grass.upload(ring, slot).ok());
        }
    }

    std::vector<u8> render(GrassQuality quality, f32 wind = 0.0f, std::span<const Vec4> benders = {},
                           std::span<const Vec4> shorteners = {})
    {
        gl.device->bindFramebuffer(&target);
        gl.device->setViewport(0, 0, kSize, kSize);
        gl.device->clear(Vec4(0, 0, 0, 1), 0.0f);
        const Environment light{.sunIntensity = 0.0f, .ambientSky = Vec3(1.0f), .ambientGround = Vec3(1.0f)};
        const LightList none;
        meshes.setLighting(*gl.device, light, none);
        meshes.bindLighting(*gl.device);
        GrassFrame frame;
        frame.settings = grassSettings(quality);
        frame.layers = layers;
        frame.wind.strength = wind;
        frame.wind.time = 1.3f;
        frame.benders = benders;
        frame.shorteners = shorteners;
        grass.draw(*gl.device, camera, ring, frame, &none);
        return gl.device->readPixels(0, 0, kSize, kSize, &target);
    }
};

/// Lit pixels in the columns [from, to) of the lower two thirds (the ground near the camera).
u32 lit(const std::vector<u8>& rgba, u32 from, u32 to)
{
    u32 count = 0;
    for (u32 y = 0; y < kSize * 2 / 3; ++y)
    {
        for (u32 x = from; x < to; ++x)
        {
            const u8* p = &rgba[(static_cast<usize>(y) * kSize + x) * 4];
            count += p[0] + p[1] + p[2] > 0 ? 1 : 0;
        }
    }
    return count;
}
} // namespace

TEST_CASE("Grass GPU: tufts on the meadow, none on the cobbles, none with the quality off")
{
    GrassScene scene;
    scene.fill(GrassQuality::Medium);
    REQUIRE_FALSE(scene.ring.tiles().empty());
    const std::vector<u8> image = scene.render(GrassQuality::Medium);
    const u32 meadow = lit(image, 0, kSize / 2 - 4);
    const u32 cobbles = lit(image, kSize / 2 + 4, kSize);
    CHECK(meadow > 80);           // loose tufts (decision 1), the ground shows through
    CHECK(cobbles < meadow / 20); // blades leaning over the edge at most
    CHECK(scene.grass.stats().nearTufts > 0);
    CHECK(scene.grass.stats().farTufts > 0);
    CHECK(scene.grass.stats().triangles > 0);

    scene.fill(GrassQuality::Off);
    CHECK(scene.ring.tiles().empty());
    const std::vector<u8> off = scene.render(GrassQuality::Off);
    CHECK(lit(off, 0, kSize) == 0);
    CHECK(scene.grass.stats().nearTufts == 0);
    CHECK(scene.gl.device->debugErrorCount() == 0);
}

TEST_CASE("Grass GPU: the wind moves the blades, without it they stand")
{
    GrassScene scene;
    scene.fill(GrassQuality::Medium);
    const std::vector<u8> still = scene.render(GrassQuality::Medium, 0.0f);
    const std::vector<u8> again = scene.render(GrassQuality::Medium, 0.0f);
    const std::vector<u8> windy = scene.render(GrassQuality::Medium, 1.0f);
    CHECK(still == again);
    usize changed = 0;
    for (usize i = 0; i < still.size(); i += 4)
    {
        changed += still[i] != windy[i] || still[i + 1] != windy[i + 1] ? 1 : 0;
    }
    CHECK(changed > 50);
    CHECK(scene.gl.device->debugErrorCount() == 0);
}

TEST_CASE("Grass GPU: blades bend away from a figure; tufts around lying items are cut short")
{
    GrassScene scene;
    scene.fill(GrassQuality::Medium);
    const std::vector<u8> plain = scene.render(GrassQuality::Medium);
    // A figure on the meadow in front of the camera: the blades there move, the image elsewhere stays.
    const std::vector<Vec4> figure{Vec4(-1.0f, 0.0f, 3.0f, kGrassBendRadius)};
    const std::vector<u8> bent = scene.render(GrassQuality::Medium, 0.0f, figure);
    CHECK(bent == scene.render(GrassQuality::Medium, 0.0f, figure)); // stable
    usize changed = 0;
    for (usize i = 0; i < plain.size(); i += 4)
    {
        changed += plain[i] != bent[i] || plain[i + 1] != bent[i + 1] ? 1 : 0;
    }
    CHECK(changed > 10);
    // Items lying all over the near meadow: a third of the height, far fewer grass pixels.
    std::vector<Vec4> items;
    for (f32 x = -4.0f; x < 0.0f; x += 0.5f)
    {
        for (f32 z = 0.0f; z < 4.0f; z += 0.5f)
        {
            items.emplace_back(x, 0.0f, z, kGrassItemRadius);
        }
    }
    items.resize(kGrassMaxShorteners);
    const std::vector<u8> cut = scene.render(GrassQuality::Medium, 0.0f, {}, items);
    MESSAGE("meadow pixels plain ", lit(plain, 0, kSize / 2), " cut ", lit(cut, 0, kSize / 2));
    CHECK(lit(cut, 0, kSize / 2) * 20 < lit(plain, 0, kSize / 2) * 19);
    CHECK(scene.gl.device->debugErrorCount() == 0);
}
