// Skinned meshes on the GPU (M6 part C, label "gpu"): bones move the vertices in the shader, morph targets
// on the CPU, the shadow pass draws them too.

#include "GlFixture.hpp"

#include <g7/core/FileSystem.hpp>
#include <g7/render/Camera.hpp>
#include <g7/render/Material.hpp>
#include <g7/render/ShaderLibrary.hpp>
#include <g7/render/Shadows.hpp>
#include <g7/render/SkinnedMesh.hpp>

#include <vector>

using namespace g7;
using namespace g7::render;
using namespace g7::render::rhi;
using g7::test::GlFixture;
using g7::test::require;

namespace
{
/// A 2 x 2 m red quad facing +Z, all on bone 1 (child of bone 0), with one morph target that moves it +1.5 x.
asset::SkinnedModelData skinnedQuad()
{
    asset::SkinnedModelData model;
    model.skeleton.names = {"root", "body"};
    model.skeleton.parents = {-1, 0};
    model.skeleton.translations = {Vec3(0.0f), Vec3(0.0f)};
    model.skeleton.rotations = {Quat(1, 0, 0, 0), Quat(1, 0, 0, 0)};
    model.skeleton.scales = {Vec3(1.0f), Vec3(1.0f)};
    model.inverseBind = {Mat4(1.0f), Mat4(1.0f)};
    asset::SkinnedPartData part;
    part.node = "body_lod0";
    part.role = "body";
    const Vec3 n(0, 0, 1);
    part.vertices = {{Vec3(-1, -1, 0), n, Vec2(0, 0), Vec4(0)},
                     {Vec3(1, -1, 0), n, Vec2(1, 0), Vec4(0)},
                     {Vec3(1, 1, 0), n, Vec2(1, 1), Vec4(0)},
                     {Vec3(-1, 1, 0), n, Vec2(0, 1), Vec4(0)}};
    part.joints.assign(4, std::array<u16, 4>{1, 0, 0, 0});
    part.weights.assign(4, Vec4(1, 0, 0, 0));
    part.indices = {0, 1, 2, 0, 2, 3};
    part.submeshes = {{0, 6, 0}};
    part.morphs.push_back({"", std::vector<Vec3>(4, Vec3(1.5f, 0.0f, 0.0f)), {}});
    model.parts.push_back(part);
    model.materials = {{"red", Vec4(1, 0, 0, 1), -1}};
    model.bounds = AABB{Vec3(-1, -1, 0), Vec3(1, 1, 0)};
    return model;
}
} // namespace

TEST_CASE("Skinned mesh: bones and morph targets move the vertices, shadows draw")
{
    GlFixture gl;
    const asset::SkinnedModelData model = skinnedQuad();
    SkinnedMesh mesh = require(SkinnedMesh::create(*gl.device, model, 0));
    CHECK(mesh.morphCount() == 1);
    CHECK(mesh.submeshes().size() == 1);
    CHECK_FALSE(SkinnedMesh::create(*gl.device, asset::SkinnedModelData{}, 0).ok());

    ShaderLibrary library(*gl.device, fs::fromUtf8(G7_SHADER_DIR));
    MeshRenderer renderer = require(MeshRenderer::create(*gl.device, library, 1.0f));
    asset::MeshData materialsOnly;
    materialsOnly.materials = model.materials;
    MaterialSet materials = require(MaterialSet::create(*gl.device, materialsOnly, fs::Path(".")));

    Texture color = require(gl.device->createTexture({16, 16, Format::RGBA8, 1}));
    Texture depth = require(gl.device->createTexture({16, 16, Format::Depth32F, 1}));
    Framebuffer target = require(gl.device->createFramebuffer({{&color}, &depth}));
    Camera camera;
    camera.aspect = 1.0f;
    camera.transform.position = Vec3(0, 0, 3);
    const auto render = [&](const std::vector<Mat4>& bones)
    {
        gl.device->bindFramebuffer(&target);
        gl.device->setViewport(0, 0, 16, 16);
        gl.device->clear(Vec4(0, 0, 0, 1), 0.0f);
        renderer.drawSkinned(*gl.device, mesh, materials, Mat4(1.0f), bones, camera);
    };
    const auto red = [&](i32 x) { return gl.device->readPixels(x, 8, 1, 1, &target)[0] > 128; };

    render({Mat4(1.0f), Mat4(1.0f)});
    CHECK(red(8));
    // Bone 1 moves 1.5 m to the right: the centre is empty, the right side red.
    render({Mat4(1.0f), glm::translate(Mat4(1.0f), Vec3(1.5f, 0, 0))});
    CHECK_FALSE(red(8));
    CHECK(red(13));
    // Bone 0 alone changes nothing (no vertex uses it).
    render({glm::translate(Mat4(1.0f), Vec3(1.5f, 0, 0)), Mat4(1.0f)});
    CHECK(red(8));
    // The morph target does the same as the bone; weight 0 brings it back.
    const f32 one = 1.0f;
    mesh.setMorphWeights(std::span(&one, 1));
    render({Mat4(1.0f), Mat4(1.0f)});
    CHECK_FALSE(red(8));
    CHECK(red(13));
    mesh.setMorphWeights({});
    render({Mat4(1.0f), Mat4(1.0f)});
    CHECK(red(8));

    // By name (M10, figuren's beards): the target "vis_aa" follows weight 1 of the list, not weight 0.
    asset::SkinnedModelData named = model;
    named.parts[0].morphs[0].name = "vis_aa";
    mesh = require(SkinnedMesh::create(*gl.device, named, 0));
    const std::string_view names[] = {"blink_l", "vis_aa"};
    mesh.mapMorphNames(names);
    const f32 first[] = {1.0f, 0.0f};
    mesh.setMorphWeights(first);
    render({Mat4(1.0f), Mat4(1.0f)});
    CHECK(red(8)); // weight 0 drives blink_l: this part has none
    const f32 second[] = {0.0f, 1.0f};
    mesh.setMorphWeights(second);
    render({Mat4(1.0f), Mat4(1.0f)});
    CHECK_FALSE(red(8));
    CHECK(red(13));
    mesh.setMorphWeights({});

    // Shadow pass: depth only, no GL errors.
    ShadowMap shadows = require(ShadowMap::create(*gl.device, ShadowSettings{}));
    const auto cascades = computeCascades(camera, Vec3(0.3f, 1.0f, 0.2f), shadows.settings());
    shadows.begin(*gl.device);
    shadows.beginCascade(*gl.device, 0);
    renderer.drawShadowSkinned(*gl.device, mesh, materials, Mat4(1.0f),
                               std::vector<Mat4>{Mat4(1.0f), Mat4(1.0f)}, cascades[0]);
    CHECK(gl.device->debugErrorCount() == 0);
}
