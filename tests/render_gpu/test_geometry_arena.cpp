// Geometry arena (label "gpu"): many meshes in shared blocks, ranges reused, same image as meshes
// with buffers of their own; the device skips re-attaching the same buffers.

#include "GlFixture.hpp"

#include <g7/render/Camera.hpp>
#include <g7/render/GeometryArena.hpp>
#include <g7/render/Lighting.hpp>
#include <g7/render/Material.hpp>
#include <g7/render/Mesh.hpp>
#include <g7/render/ShaderLibrary.hpp>

#include <vector>

using namespace g7;
using namespace g7::render;
using namespace g7::render::rhi;
using g7::test::GlFixture;
using g7::test::require;

namespace
{
asset::MeshData quad(f32 size)
{
    asset::MeshData data;
    const f32 h = size * 0.5f;
    for (const Vec2 p : {Vec2(-h, -h), Vec2(h, -h), Vec2(h, h), Vec2(-h, h)})
    {
        asset::Vertex v{};
        v.position = Vec3(p.x, p.y, 0.0f);
        v.normal = Vec3(0.0f, 0.0f, 1.0f);
        v.tangent = Vec4(1.0f, 0.0f, 0.0f, 1.0f);
        data.vertices.push_back(v);
    }
    data.indices = {0, 1, 2, 0, 2, 3};
    data.submeshes = {{0, 6, 0}};
    data.materials.push_back({});
    data.bounds = AABB{Vec3(-h, -h, 0.0f), Vec3(h, h, 0.0f)};
    return data;
}
} // namespace

TEST_CASE("Geometry arena: meshes share a block, ranges come back and are reused")
{
    GlFixture gl;
    GeometryArena arena;
    const asset::MeshData data = quad(1.0f);
    std::vector<Mesh> meshes;
    for (int i = 0; i < 100; ++i)
    {
        meshes.push_back(require(Mesh::create(*gl.device, arena, data)));
    }
    CHECK(arena.blockCount() == 1);
    CHECK(arena.usedVertices() == 400);
    CHECK(arena.usedIndices() == 600);

    meshes.erase(meshes.begin() + 10, meshes.begin() + 20); // a hole in the middle
    CHECK(arena.usedVertices() == 360);
    meshes.push_back(require(Mesh::create(*gl.device, arena, data))); // fills the hole
    CHECK(arena.usedVertices() == 364);
    meshes.clear();
    CHECK(arena.usedVertices() == 0);
    CHECK(arena.usedIndices() == 0);

    // Freed ranges merged again: a mesh as large as a whole block fits into the first block.
    asset::MeshData big = data;
    big.vertices.resize(GeometryArena::kBlockVertices, data.vertices[0]);
    {
        Mesh whole = require(Mesh::create(*gl.device, arena, big));
        CHECK(arena.blockCount() == 1);
        // Another mesh needs a second block now; one larger than a block gets a block of its own.
        Mesh second = require(Mesh::create(*gl.device, arena, data));
        CHECK(arena.blockCount() == 2);
        big.vertices.resize(GeometryArena::kBlockVertices + 10, data.vertices[0]);
        Mesh huge = require(Mesh::create(*gl.device, arena, big));
        CHECK(arena.blockCount() == 3);
    }
    CHECK(arena.usedVertices() == 0);
    CHECK_FALSE(Mesh::create(*gl.device, arena, asset::MeshData{}).ok()); // no geometry
    CHECK(gl.device->debugErrorCount() == 0);
}

TEST_CASE("Geometry arena: arena meshes look like meshes with buffers of their own")
{
    GlFixture gl;
    ShaderLibrary library(*gl.device, fs::fromUtf8(G7_SHADER_DIR));
    MeshRenderer renderer = require(MeshRenderer::create(*gl.device, library, 1.0f));
    GeometryArena arena;
    // Two different quads in the arena: the second one starts at vertex 4 (drawn with baseVertex).
    const asset::MeshData small = quad(0.5f);
    const asset::MeshData large = quad(1.5f);
    Mesh arenaSmall = require(Mesh::create(*gl.device, arena, small));
    Mesh arenaLarge = require(Mesh::create(*gl.device, arena, large));
    Mesh ownLarge = require(Mesh::create(*gl.device, large));
    const MaterialSet materials = require(MaterialSet::create(*gl.device, large, MaterialSet::ImageLookup{}));

    Texture color = require(gl.device->createTexture({32, 32, rhi::Format::RGBA8, 1}));
    Texture depth = require(gl.device->createTexture({32, 32, rhi::Format::Depth32F, 1}));
    Framebuffer target = require(gl.device->createFramebuffer({{&color}, &depth}));
    Camera camera;
    camera.aspect = 1.0f;
    camera.transform.position = Vec3(0.0f, 0.0f, 2.0f);
    const auto render = [&](const Mesh& mesh)
    {
        gl.device->bindFramebuffer(&target);
        gl.device->setViewport(0, 0, 32, 32);
        gl.device->clear(Vec4(0, 0, 0, 1), 0.0f);
        const Environment light{.sunIntensity = 0.0f, .ambientSky = Vec3(1.0f), .ambientGround = Vec3(1.0f)};
        const LightList none;
        renderer.setLighting(*gl.device, light, none);
        renderer.draw(*gl.device, mesh, materials, Mat4(1.0f), camera);
        return gl.device->readPixels(0, 0, 32, 32, &target);
    };
    const auto own = render(ownLarge);
    CHECK(render(arenaLarge) == own);
    CHECK(render(arenaSmall) != own); // the smaller quad really is another range

    // Both arena meshes live in one block: switching between them attaches nothing new.
    gl.device->beginFrame(32, 32, Vec4(0.0f));
    render(arenaSmall);
    render(arenaLarge);
    render(arenaSmall);
    CHECK(gl.device->stats().bufferBinds <= 2); // once vertices and indices for the pipeline, then cached
    CHECK(gl.device->debugErrorCount() == 0);
}
