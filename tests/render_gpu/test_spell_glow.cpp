// Spell glow on skinned figures (M17 part G, label "gpu"): the hand glows only where its bones weigh, the
// flat fill lights the whole figure, no glow leaves the image as it was.

#include "GlFixture.hpp"

#include <g7/core/FileSystem.hpp>
#include <g7/render/Camera.hpp>
#include <g7/render/Material.hpp>
#include <g7/render/ShaderLibrary.hpp>
#include <g7/render/SkinnedMesh.hpp>
#include <g7/render/SpellFx.hpp>

#include <vector>

using namespace g7;
using namespace g7::render;
using namespace g7::render::rhi;
using g7::test::GlFixture;
using g7::test::require;

namespace
{
constexpr i32 kSize = 32;

/// Two dark red quads side by side facing +Z: the left one on bone 1 ("hand"), the right one on bone 0.
asset::SkinnedModelData twoQuads()
{
    asset::SkinnedModelData model;
    model.skeleton.names = {"root", "hand"};
    model.skeleton.parents = {-1, 0};
    model.skeleton.translations = {Vec3(0.0f), Vec3(0.0f)};
    model.skeleton.rotations = {Quat(1, 0, 0, 0), Quat(1, 0, 0, 0)};
    model.skeleton.scales = {Vec3(1.0f), Vec3(1.0f)};
    model.inverseBind = {Mat4(1.0f), Mat4(1.0f)};
    asset::SkinnedPartData part;
    part.node = "body_lod0";
    part.role = "body";
    const Vec3 n(0, 0, 1);
    for (const f32 x : {-1.0f, 0.05f})
    {
        const u32 base = static_cast<u32>(part.vertices.size());
        part.vertices.push_back({Vec3(x, -1, 0), n, Vec2(0, 0), Vec4(0)});
        part.vertices.push_back({Vec3(x + 0.95f, -1, 0), n, Vec2(1, 0), Vec4(0)});
        part.vertices.push_back({Vec3(x + 0.95f, 1, 0), n, Vec2(1, 1), Vec4(0)});
        part.vertices.push_back({Vec3(x, 1, 0), n, Vec2(0, 1), Vec4(0)});
        const u16 bone = x < 0.0f ? 1 : 0;
        for (int i = 0; i < 4; ++i)
        {
            part.joints.push_back({bone, 0, 0, 0});
            part.weights.push_back(Vec4(1, 0, 0, 0));
        }
        for (const u32 i : {0u, 1u, 2u, 0u, 2u, 3u})
        {
            part.indices.push_back(base + i);
        }
    }
    part.submeshes = {{0, 12, 0}};
    model.parts.push_back(part);
    model.materials = {{"cloth", Vec4(0.3f, 0.05f, 0.05f, 1), -1}};
    model.bounds = AABB{Vec3(-1, -1, 0), Vec3(1, 1, 0)};
    return model;
}

/// Blue (channel 2) at a pixel row in the middle.
u8 blueAt(const std::vector<u8>& pixels, i32 x)
{
    return pixels[static_cast<usize>(((kSize / 2) * kSize + x) * 4 + 2)];
}
} // namespace

TEST_CASE("Spell glow GPU: the hand glows where its bones weigh, the fill everywhere, none changes nothing")
{
    GlFixture gl;
    const asset::SkinnedModelData model = twoQuads();
    SkinnedMesh mesh = require(SkinnedMesh::create(*gl.device, model, 0));
    ShaderLibrary library(*gl.device, fs::fromUtf8(G7_SHADER_DIR));
    MeshRenderer renderer = require(MeshRenderer::create(*gl.device, library, 1.0f));
    asset::MeshData materialsOnly;
    materialsOnly.materials = model.materials;
    MaterialSet materials = require(MaterialSet::create(*gl.device, materialsOnly, fs::Path(".")));
    Texture color = require(gl.device->createTexture({kSize, kSize, Format::RGBA8, 1}));
    Texture depth = require(gl.device->createTexture({kSize, kSize, Format::Depth32F, 1}));
    Framebuffer target = require(gl.device->createFramebuffer({{&color}, &depth}));
    Camera camera;
    camera.aspect = 1.0f;
    camera.transform.position = Vec3(0, 0, 2.5f);
    const std::vector<Mat4> bones(2, Mat4(1.0f));
    const auto render = [&](const FigureGlow& glow)
    {
        gl.device->bindFramebuffer(&target);
        gl.device->setViewport(0, 0, kSize, kSize);
        gl.device->clear(Vec4(0, 0, 0, 1), 0.0f);
        renderer.drawSkinned(*gl.device, mesh, materials, Mat4(1.0f), bones, camera, glow);
        return gl.device->readPixels(0, 0, kSize, kSize, &target);
    };
    const auto plain = render({});
    const i32 left = kSize / 4;
    const i32 right = 3 * kSize / 4;
    REQUIRE(blueAt(plain, left) < 40);
    REQUIRE(blueAt(plain, right) < 40);

    FigureGlow hand;
    hand.hand = Vec3(0.0f, 0.0f, 0.8f);
    hand.handBones = {1u << 1, 0, 0, 0}; // bone 1
    const auto glowing = render(hand);
    CHECK(blueAt(glowing, left) > 150);                    // the hand's bone
    CHECK(blueAt(glowing, right) == blueAt(plain, right)); // the rest stays

    FigureGlow fill;
    fill.fill = Vec3(0.0f, 0.0f, 0.5f);
    const auto flashed = render(fill);
    CHECK(blueAt(flashed, left) > 100);
    CHECK(blueAt(flashed, right) > 100);

    CHECK(render({}) == plain);
    CHECK(gl.device->debugErrorCount() == 0);
}
