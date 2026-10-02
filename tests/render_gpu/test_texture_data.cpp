// Materials with asset::TextureData (ADR 0016) on a real driver (label "gpu"): BC5 normal maps light
// like their RGB equivalent, BC7 colour, RGBA8 used as colour and as data.

#include "GlFixture.hpp"

#include <g7/asset/TextureData.hpp>
#include <g7/render/Camera.hpp>
#include <g7/render/Lighting.hpp>
#include <g7/render/Material.hpp>
#include <g7/render/Mesh.hpp>
#include <g7/render/ShaderLibrary.hpp>
#include <g7/render/TextureUpload.hpp>

#include <array>
#include <cmath>

using namespace g7;
using namespace g7::render;
using namespace g7::render::rhi;
using g7::test::GlFixture;
using g7::test::require;

namespace
{
/// 2x2 m quad facing +Z with tangents; material 0 uses image 0 as normal map and image 1 as colour.
asset::MeshData texturedQuad(bool normalMap, bool colourMap)
{
    asset::MeshData data;
    const Vec3 n(0, 0, 1);
    const Vec4 t(1, 0, 0, 1);
    data.vertices = {{Vec3(-1, -1, 0), n, Vec2(0, 1), t},
                     {Vec3(1, -1, 0), n, Vec2(1, 1), t},
                     {Vec3(1, 1, 0), n, Vec2(1, 0), t},
                     {Vec3(-1, 1, 0), n, Vec2(0, 0), t}};
    data.indices = {0, 1, 2, 0, 2, 3};
    data.submeshes = {{0, 6, 0}};
    asset::MaterialInfo material;
    material.normalImage = normalMap ? 0 : -1;
    material.baseColorImage = colourMap ? 1 : -1;
    data.materials = {material};
    data.images = {asset::ImageSource{"normal", {}}, asset::ImageSource{"colour", {}}};
    data.bounds = AABB{Vec3(-1, -1, 0), Vec3(1, 1, 0)};
    return data;
}

/// 4x4 BC5 texture: every texel X = x8, Y = y8 (BC4 halves with both endpoints equal).
asset::TextureData bc5(u8 x8, u8 y8)
{
    std::vector<u8> block(16, 0);
    block[0] = block[1] = x8;
    block[8] = block[9] = y8;
    return {asset::TextureFormat::BC5, false, {{4, 4, block}}};
}

/// 4x4 RGBA8 texture of one value.
asset::TextureData rgba8(u8 r, u8 g, u8 b, u8 a, bool srgb = false)
{
    std::vector<u8> pixels;
    for (int i = 0; i < 16; ++i)
    {
        pixels.insert(pixels.end(), {r, g, b, a});
    }
    return {asset::TextureFormat::RGBA8, srgb, {{4, 4, pixels}}};
}

/// BC7 mode 6 block of one colour (see test_compressed.cpp): red 255, green/blue 1, alpha 255.
asset::TextureData bc7Red()
{
    std::array<u8, 16> bytes{};
    u32 position = 0;
    const auto put = [&](u32 value, u32 bits)
    {
        for (u32 i = 0; i < bits; ++i, ++position)
        {
            if ((value >> i) & 1u)
            {
                bytes[position / 8] |= static_cast<u8>(1u << (position % 8));
            }
        }
    };
    put(1u << 6, 7);
    for (const u32 channel : {127u, 0u, 0u, 127u})
    {
        put(channel, 7);
        put(channel, 7);
    }
    put(1, 1);
    put(1, 1);
    put(0, 63);
    return {asset::TextureFormat::BC7, true, {{4, 4, std::vector<u8>(bytes.begin(), bytes.end())}}};
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

    MaterialSet materials(const asset::MeshData& data, const asset::TextureData* normal,
                          const asset::TextureData* colour)
    {
        return require(MaterialSet::create(*gl.device, data,
                                           [&](const asset::ImageSource& source) -> const asset::TextureData*
                                           { return source.uri == "normal" ? normal : colour; }));
    }

    /// Centre pixel of the quad lit by `environment`.
    std::array<u8, 4> centre(const asset::MeshData& data, const MaterialSet& set,
                             const Environment& environment)
    {
        Mesh mesh = require(Mesh::create(*gl.device, data));
        Camera camera;
        camera.aspect = 1.0f;
        camera.transform.position = Vec3(0, 0, 2.5f);
        gl.device->bindFramebuffer(&target);
        gl.device->setViewport(0, 0, 16, 16);
        gl.device->clear(Vec4(0, 0, 0, 1), 0.0f);
        const LightList none;
        renderer.setLighting(*gl.device, environment, none);
        renderer.draw(*gl.device, mesh, set, Mat4(1.0f), camera);
        const auto p = gl.device->readPixels(8, 8, 1, 1, &target);
        return {p[0], p[1], p[2], p[3]};
    }
};

Environment slantedSun()
{
    // Sun from the right and the front: a normal tilted towards +X catches more light.
    return Environment{.sunDirection = glm::normalize(Vec3(1, 0, 1)),
                       .sunColor = Vec3(1.0f),
                       .sunIntensity = 0.8f,
                       .ambientSky = Vec3(0.0f),
                       .ambientGround = Vec3(0.0f)};
}
} // namespace

TEST_CASE("TextureData: a BC5 normal map lights like the same normal stored as RGB")
{
    Scene scene;
    const asset::MeshData quad = texturedQuad(true, false);
    // Normal tilted towards +X: x = 191/255*2-1 = 0.498, y ~ 0, z = sqrt(1 - x*x) = 0.867.
    const asset::TextureData twoChannel = bc5(191, 128);
    const u8 z8 = static_cast<u8>(std::lround((std::sqrt(1.0 - 0.498 * 0.498) + 1.0) * 0.5 * 255.0));
    const asset::TextureData rgb = rgba8(191, 128, z8, 255);
    const asset::TextureData flat = rgba8(128, 128, 255, 255);

    const MaterialSet bc5Set = scene.materials(quad, &twoChannel, nullptr);
    const MaterialSet rgbSet = scene.materials(quad, &rgb, nullptr);
    const MaterialSet flatSet = scene.materials(quad, &flat, nullptr);
    CHECK(bc5Set[0].normalTwoChannel);
    CHECK(bc5Set[0].normal->desc().format == Format::BC5);
    CHECK_FALSE(rgbSet[0].normalTwoChannel);
    CHECK(rgbSet[0].normal->desc().format ==
          Format::RGBA8); // data map: linear even from an sRGB-flagged image

    const auto lit = [&](const MaterialSet& set) { return int(scene.centre(quad, set, slantedSun())[0]); };
    const int twoChannelLight = lit(bc5Set);
    const int rgbLight = lit(rgbSet);
    const int flatLight = lit(flatSet);
    CHECK(std::abs(twoChannelLight - rgbLight) <= 3);
    CHECK(twoChannelLight > flatLight + 10); // the tilt matters, so the comparison means something
    CHECK(scene.gl.device->debugErrorCount() == 0);
}

TEST_CASE("TextureData: BC7 colour is sRGB as cooked")
{
    Scene scene;
    const asset::MeshData quad = texturedQuad(false, true);
    const asset::TextureData red = bc7Red();
    const MaterialSet set = scene.materials(quad, nullptr, &red);
    CHECK(set[0].baseColor->desc().format == Format::BC7_SRGB);
    const Environment white{.sunIntensity = 0.0f, .ambientSky = Vec3(1.0f), .ambientGround = Vec3(1.0f)};
    const auto pixel = scene.centre(quad, set, white);
    CHECK(pixel[0] > 240);
    CHECK(pixel[1] < 10);
    CHECK(scene.gl.device->debugErrorCount() == 0);
}

TEST_CASE("TextureData: one RGBA8 image used as colour and as normal map gets two uploads")
{
    Scene scene;
    asset::MeshData quad = texturedQuad(true, true);
    quad.materials[0].baseColorImage = 0; // the same image for both uses
    const asset::TextureData image = rgba8(128, 128, 255, 255, true);
    const MaterialSet set = scene.materials(quad, &image, &image);
    CHECK(set[0].baseColor->desc().format == Format::RGBA8_SRGB);
    CHECK(set[0].normal->desc().format == Format::RGBA8);
    CHECK(set[0].baseColor != set[0].normal);
    CHECK(set[0].baseColor->desc().mipLevels == 3); // 4x4: generated chain
}

TEST_CASE("TextureData: invalid data is rejected")
{
    GlFixture gl;
    CHECK_FALSE(createTexture(*gl.device, asset::TextureData{}, true).ok()); // no levels
    asset::TextureData wrongLevel = bc5(0, 0);
    wrongLevel.levels.push_back({1, 1, std::vector<u8>(16)}); // level 1 of 4x4 must be 2x2
    CHECK_FALSE(createTexture(*gl.device, wrongLevel, false).ok());
    asset::TextureData tooMany = rgba8(0, 0, 0, 255);
    for (int i = 0; i < 4; ++i)
    {
        tooMany.levels.push_back({1, 1, std::vector<u8>(4)});
    }
    CHECK_FALSE(createTexture(*gl.device, tooMany, true).ok());
    asset::TextureData mipped = bc5(10, 20);
    mipped.levels.push_back({2, 2, std::vector<u8>(16)});
    mipped.levels.push_back({1, 1, std::vector<u8>(16)});
    const Texture texture = require(createTexture(*gl.device, mipped, false));
    CHECK(texture.desc().mipLevels == 3);
    CHECK(gl.device->debugErrorCount() == 0);
}
