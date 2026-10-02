// Materials on a real driver (label "gpu"): alpha test, double-sided, normal map, emissive, blend.

#include "GlFixture.hpp"

#include <g7/render/Camera.hpp>
#include <g7/render/Material.hpp>
#include <g7/render/Mesh.hpp>
#include <g7/render/ShaderLibrary.hpp>

#if defined(_MSC_VER)
#pragma warning(push, 0)
#endif
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STBI_WRITE_NO_STDIO
#include <stb_image_write.h>
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

#include <array>
#include <cstdlib>

using namespace g7;
using namespace g7::render;
using namespace g7::render::rhi;
using g7::test::GlFixture;
using g7::test::require;

namespace
{
void appendBytes(void* context, void* data, int size)
{
    auto* out = static_cast<std::vector<u8>*>(context);
    out->insert(out->end(), static_cast<const u8*>(data), static_cast<const u8*>(data) + size);
}

asset::ImageSource png1x1(u8 r, u8 g, u8 b, u8 a)
{
    const u8 pixel[4] = {r, g, b, a};
    asset::ImageSource source;
    REQUIRE(stbi_write_png_to_func(appendBytes, &source.encoded, 1, 1, 4, pixel, 4) != 0);
    source.mimeType = "image/png";
    return source;
}

/// 2x2 m quad facing +Z with tangents along +X.
asset::MeshData quad(const asset::MaterialInfo& material, std::vector<asset::ImageSource> images = {})
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
    data.materials = {material};
    data.images = std::move(images);
    data.bounds = AABB{Vec3(-1, -1, 0), Vec3(1, 1, 0)};
    return data;
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

    /// Renders the quad (camera on +Z looking at it, or from behind) and returns the centre pixel.
    std::array<u8, 4> centre(const asset::MeshData& data, bool fromBehind = false)
    {
        Mesh mesh = require(Mesh::create(*gl.device, data));
        MaterialSet materials = require(MaterialSet::create(*gl.device, data, fs::Path(".")));
        Camera camera;
        camera.aspect = 1.0f;
        camera.transform.position = Vec3(0, 0, fromBehind ? -2.5f : 2.5f);
        camera.transform.rotation = fromBehind ? quatFromEuler(0.0f, kPi, 0.0f) : Quat(1, 0, 0, 0);
        gl.device->bindFramebuffer(&target);
        gl.device->setViewport(0, 0, 16, 16);
        gl.device->clear(Vec4(0, 0, 0, 1), 0.0f);
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

TEST_CASE("Material: alpha test discards below the cutoff")
{
    Scene scene;
    asset::MaterialInfo leaf;
    leaf.alphaMode = asset::AlphaMode::Mask;
    leaf.baseColorImage = 0;
    CHECK(brightness(scene.centre(quad(leaf, {png1x1(255, 255, 255, 0)}))) == 0);    // hole: background
    CHECK(brightness(scene.centre(quad(leaf, {png1x1(255, 255, 255, 255)}))) > 300); // solid leaf
    leaf.alphaCutoff = 0.0f;
    CHECK(brightness(scene.centre(quad(leaf, {png1x1(255, 255, 255, 0)}))) > 300); // cutoff 0 keeps all
    CHECK(scene.gl.device->debugErrorCount() == 0);
}

TEST_CASE("Material: back faces only for double-sided materials")
{
    Scene scene;
    asset::MaterialInfo single;
    CHECK(brightness(scene.centre(quad(single), true)) == 0); // culled
    asset::MaterialInfo twoSided;
    twoSided.doubleSided = true;
    CHECK(brightness(scene.centre(quad(twoSided), true)) > 0);
    CHECK(scene.gl.device->debugErrorCount() == 0);
}

TEST_CASE("Material: normal maps")
{
    Scene scene;
    asset::MaterialInfo plain;
    const auto reference = scene.centre(quad(plain));

    asset::MaterialInfo flat;
    flat.normalImage = 0;
    const auto withFlat = scene.centre(quad(flat, {png1x1(128, 128, 255, 255)}));
    for (int c = 0; c < 3; ++c)
    {
        CHECK(std::abs(int(withFlat[c]) - int(reference[c])) <= 2); // flat map = no map
    }

    // Normal tilted towards +Y (tangent space up), i.e. towards the light above: brighter.
    asset::MaterialInfo tilted;
    tilted.normalImage = 0;
    const auto up = scene.centre(quad(tilted, {png1x1(128, 230, 180, 255)}));
    CHECK(brightness(up) > brightness(reference) + 10);
    tilted.normalScale = 0.0f; // scale 0 cancels the map
    const auto cancelled = scene.centre(quad(tilted, {png1x1(128, 230, 180, 255)}));
    CHECK(std::abs(brightness(cancelled) - brightness(reference)) <= 6);
    CHECK(scene.gl.device->debugErrorCount() == 0);
}

TEST_CASE("Material: emissive adds light independent of shading")
{
    Scene scene;
    asset::MaterialInfo glow;
    glow.baseColor = Vec4(0, 0, 0, 1);
    glow.emissive = Vec3(1, 0, 0);
    const auto p = scene.centre(quad(glow));
    CHECK(p[0] == 255);
    CHECK(p[1] == 0);
    CHECK(p[2] == 0);

    glow.emissiveImage = 0; // emissive texture modulates the factor
    const auto masked = scene.centre(quad(glow, {png1x1(0, 0, 0, 255)}));
    CHECK(masked[0] == 0);
    CHECK(scene.gl.device->debugErrorCount() == 0);
}

TEST_CASE("Material: blend mixes with the background")
{
    Scene scene;
    asset::MaterialInfo opaque;
    const auto full = scene.centre(quad(opaque));
    asset::MaterialInfo glass;
    glass.alphaMode = asset::AlphaMode::Blend;
    glass.baseColor = Vec4(1, 1, 1, 0.5f);
    const auto half = scene.centre(quad(glass));
    CHECK(std::abs(int(half[0]) - int(full[0]) / 2) <= 3); // 50 % over black
    CHECK(scene.gl.device->debugErrorCount() == 0);
}
