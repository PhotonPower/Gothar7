// Mesh upload and drawing with the engine's mesh shader (label "gpu").

#include "GlFixture.hpp"

#include <g7/core/FileSystem.hpp>
#include <g7/render/Camera.hpp>
#include <g7/render/Mesh.hpp>
#include <g7/render/ShaderLibrary.hpp>
#include <g7/runtime/Engine.hpp>

#include <chrono>
#include <filesystem>
#include <string>

using namespace g7;
using namespace g7::render;
using namespace g7::render::rhi;
using g7::test::GlFixture;
using g7::test::require;

namespace
{
/// 2x2 m quad in the XY plane at z = 0, facing +Z.
asset::MeshData quad(const Vec4& color)
{
    asset::MeshData data;
    const Vec3 n(0, 0, 1);
    data.vertices = {{Vec3(-1, -1, 0), n, Vec2(0, 0), Vec4(0)},
                     {Vec3(1, -1, 0), n, Vec2(1, 0), Vec4(0)},
                     {Vec3(1, 1, 0), n, Vec2(1, 1), Vec4(0)},
                     {Vec3(-1, 1, 0), n, Vec2(0, 1), Vec4(0)}};
    data.indices = {0, 1, 2, 0, 2, 3};
    data.submeshes = {{0, 6, 0}};
    data.materials = {{"test", color, {}}};
    data.bounds = AABB{Vec3(-1, -1, 0), Vec3(1, 1, 0)};
    return data;
}
} // namespace

TEST_CASE("Mesh: upload and draw with the mesh shader")
{
    GlFixture gl;
    Mesh mesh = require(Mesh::create(*gl.device, quad(Vec4(1, 0, 0, 1))));
    CHECK(mesh.submeshes().size() == 1);
    CHECK(mesh.bounds().max == Vec3(1, 1, 0));

    ShaderLibrary library(*gl.device, fs::fromUtf8(G7_SHADER_DIR));
    ShaderProgram* program = require(library.load("mesh", {"mesh.vert", "mesh.frag", {}}));
    PipelineDesc desc;
    desc.program = program;
    desc.attributes = Mesh::vertexLayout();
    desc.vertexStride = Mesh::kVertexStride;
    Pipeline pipeline = require(gl.device->createPipeline(desc));

    Texture color = require(gl.device->createTexture({16, 16, Format::RGBA8, 1}));
    Texture depth = require(gl.device->createTexture({16, 16, Format::Depth32F, 1}));
    Framebuffer target = require(gl.device->createFramebuffer({{&color}, &depth}));

    Camera camera;
    camera.aspect = 1.0f;
    camera.transform.position = Vec3(0, 0, 3); // looking along -Z at the quad
    program->setUniform("uViewProjection", camera.viewProjection());
    program->setUniform("uModel", Mat4(1.0f));
    program->setUniform("uBaseColor", Vec4(1, 0, 0, 1));

    gl.device->bindFramebuffer(&target);
    gl.device->setViewport(0, 0, 16, 16);
    gl.device->clear(Vec4(0, 0, 0, 1), 0.0f);
    gl.device->bindPipeline(pipeline);
    mesh.bind(*gl.device);
    mesh.draw(*gl.device, 0);

    const auto centre = gl.device->readPixels(8, 8, 1, 1, &target);
    REQUIRE(centre.size() == 4);
    // Half-Lambert with the fixed light: about 55 % of the base colour.
    CHECK(centre[0] > 110);
    CHECK(centre[0] < 170);
    CHECK(centre[1] == 0);
    CHECK(centre[2] == 0);
    const auto corner = gl.device->readPixels(0, 0, 1, 1, &target); // outside the quad
    CHECK(corner[0] == 0);
    CHECK(gl.device->debugErrorCount() == 0);

    CHECK_FALSE(Mesh::create(*gl.device, asset::MeshData{}).ok());
}

TEST_CASE("Engine: --view-mesh loads and draws a glTF model")
{
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    const fs::Path dir = std::filesystem::temp_directory_path() / ("g7_view_mesh_" + std::to_string(stamp));
    REQUIRE(fs::createDirectories(dir).ok());
    // One triangle, embedded buffer: positions (0,0,0) (1,0,0) (0,1,0).
    const std::string gltf =
        R"({"asset":{"version":"2.0"},"scene":0,"scenes":[{"nodes":[0]}],"nodes":[{"mesh":0}],)"
        R"("meshes":[{"primitives":[{"attributes":{"POSITION":0},"material":0}]}],)"
        R"("materials":[{"name":"Lehm","pbrMetallicRoughness":{"baseColorFactor":[0.6,0.4,0.2,1]}}],)"
        R"("accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3","min":[0,0,0],"max":[1,1,0]}],)"
        R"("bufferViews":[{"buffer":0,"byteLength":36}],)"
        R"("buffers":[{"byteLength":36,"uri":"data:application/octet-stream;base64,AAAAAAAAAAAAAAAAAACAPwAAAAAAAAAAAAAAAAAAgD8AAAAA"}]})";
    REQUIRE(fs::writeText(dir / "tri.gltf", gltf).ok());

    EngineConfig config;
    config.window.size = {320, 240};
    config.maxFrames = 3;
    config.shaderDirectory = fs::fromUtf8(G7_SHADER_DIR);
    config.viewMesh = dir / "tri.gltf";
    {
        Engine engine(config);
        REQUIRE(engine.init().ok());
        // The camera frames the model: its centre is in view.
        CHECK(engine.camera().frustum().contains(Vec3(0.5f, 0.5f, 0.0f)));
        CHECK(engine.run() == 0);
        CHECK(engine.renderDevice()->debugErrorCount() == 0);
    }

    config.viewMesh = dir / "missing.gltf";
    Engine broken(config);
    auto result = broken.init();
    REQUIRE_FALSE(result.ok());
    CHECK(result.error().message.find("missing.gltf") != std::string::npos);

    std::error_code ignored;
    std::filesystem::remove_all(dir, ignored);
}
