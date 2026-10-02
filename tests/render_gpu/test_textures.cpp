// Image textures on a real driver (label "gpu"): mip generation, sRGB decoding, orientation.

#include "GlFixture.hpp"

#include <g7/render/Camera.hpp>
#include <g7/render/Lighting.hpp>
#include <g7/render/Mesh.hpp>
#include <g7/render/ShaderLibrary.hpp>
#include <g7/render/TextureUpload.hpp>

#include <cstdlib>
#include <string_view>

using namespace g7;
using namespace g7::render;
using namespace g7::render::rhi;
using g7::test::GlFixture;
using g7::test::require;

namespace
{
bool near(u8 actual, int expected, int tolerance = 2)
{
    return std::abs(int(actual) - expected) <= tolerance;
}
} // namespace

TEST_CASE("Textures: full mip chain is generated from level 0")
{
    GlFixture gl;
    // 2x2 checker red/blue (linear RGBA8, so averaging is exact).
    const asset::ImageData image{2, 2, {255, 0, 0, 255, 0, 0, 255, 255, 0, 0, 255, 255, 255, 0, 0, 255}};
    Texture texture = require(createTexture(*gl.device, image, {false, true}));
    CHECK(texture.desc().mipLevels == 2);
    CHECK(texture.desc().format == Format::RGBA8);
    CHECK(gl.device->readTexture(texture, 0) == image.rgba8);
    const auto mip = gl.device->readTexture(texture, 1);
    REQUIRE(mip.size() == 4);
    CHECK(near(mip[0], 128, 1));
    CHECK(mip[1] == 0);
    CHECK(near(mip[2], 128, 1));
    CHECK(gl.device->readTexture(texture, 2).empty()); // no such level

    Texture single = require(createTexture(*gl.device, image, {true, false}));
    CHECK(single.desc().mipLevels == 1);
    CHECK(single.desc().format == Format::RGBA8_SRGB);
    CHECK_FALSE(createTexture(*gl.device, asset::ImageData{2, 2, {1, 2, 3}}).ok()); // size mismatch
    CHECK(gl.device->debugErrorCount() == 0);
}

TEST_CASE("Textures: sRGB textures are decoded to linear when sampled")
{
    GlFixture gl;
    constexpr std::string_view vs = R"(#version 450 core
void main() { const vec2 p = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2); gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0); }
)";
    constexpr std::string_view fs = R"(#version 450 core
layout(binding = 0) uniform sampler2D uTexture;
out vec4 fragColor;
void main() { fragColor = texture(uTexture, vec2(0.5)); }
)";
    ShaderProgram program = require(gl.device->createShaderProgram({vs, fs, "sample"}));
    PipelineDesc desc;
    desc.program = &program;
    desc.cull = CullMode::None;
    desc.depthTest = false;
    Pipeline pipeline = require(gl.device->createPipeline(desc));
    Texture target =
        require(gl.device->createTexture({4, 4, Format::RGBA8, 1})); // linear target: no re-encode
    Framebuffer framebuffer = require(gl.device->createFramebuffer({{&target}, nullptr}));
    Sampler sampler = require(gl.device->createSampler({}));

    const auto sample = [&](bool srgb)
    {
        Texture grey = require(createSolidTexture(*gl.device, 128, 128, 128, 255, srgb));
        gl.device->bindFramebuffer(&framebuffer);
        gl.device->setViewport(0, 0, 4, 4);
        gl.device->bindPipeline(pipeline);
        gl.device->bindTexture(0, grey, sampler);
        gl.device->draw(3);
        return gl.device->readPixels(1, 1, 1, 1, &framebuffer)[0];
    };
    CHECK(near(sample(true), 55));   // sRGB 128 = linear 0.216
    CHECK(near(sample(false), 128)); // data texture: unchanged
    CHECK(gl.device->debugErrorCount() == 0);
}

TEST_CASE("Textures: UV (0,0) is the image's top-left in the mesh shader")
{
    GlFixture gl;
    // Quad facing the camera; glTF UVs: top-left vertex has (0,0).
    asset::MeshData data;
    const Vec3 n(0, 0, 1);
    data.vertices = {{Vec3(-1, -1, 0), n, Vec2(0, 1), Vec4(0)},
                     {Vec3(1, -1, 0), n, Vec2(1, 1), Vec4(0)},
                     {Vec3(1, 1, 0), n, Vec2(1, 0), Vec4(0)},
                     {Vec3(-1, 1, 0), n, Vec2(0, 0), Vec4(0)}};
    data.indices = {0, 1, 2, 0, 2, 3};
    data.submeshes = {{0, 6, 0}};
    data.materials = {{"m", Vec4(1.0f), 0}};
    data.bounds = AABB{Vec3(-1, -1, 0), Vec3(1, 1, 0)};
    Mesh mesh = require(Mesh::create(*gl.device, data));

    // 1x2 image: first (top) row red, second row blue.
    Texture texture =
        require(createTexture(*gl.device, asset::ImageData{1, 2, {255, 0, 0, 255, 0, 0, 255, 255}}));
    SamplerDesc samplerDesc;
    samplerDesc.minFilter = samplerDesc.magFilter = samplerDesc.mipFilter = Filter::Nearest;
    samplerDesc.wrapU = samplerDesc.wrapV = Wrap::Clamp;
    Sampler sampler = require(gl.device->createSampler(samplerDesc));

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
    camera.transform.position = Vec3(0, 0, 2.5f);
    program->setUniform("uViewProjection", camera.viewProjection());
    program->setUniform("uModel", Mat4(1.0f));
    program->setUniform("uBaseColor", Vec4(1.0f));
    // Neutral lighting (white ambient only), so the output is the texel itself.
    const GpuLighting neutral =
        packLighting(Environment{.sunIntensity = 0.0f, .ambientSky = Vec3(1.0f), .ambientGround = Vec3(1.0f)},
                     LightList{});
    Buffer lighting =
        require(gl.device->createBuffer({sizeof(neutral), BufferUsage::Static,
                                         std::span(reinterpret_cast<const u8*>(&neutral), sizeof(neutral))}));
    gl.device->bindUniformBuffer(0, lighting);
    program->setUniform("uLightCount", 0);
    gl.device->bindFramebuffer(&target);
    gl.device->setViewport(0, 0, 16, 16);
    gl.device->clear(Vec4(0, 0, 0, 1), 0.0f);
    gl.device->bindPipeline(pipeline);
    mesh.bind(*gl.device);
    gl.device->bindTexture(0, texture, sampler);
    mesh.draw(*gl.device, 0);

    const auto top = gl.device->readPixels(8, 12, 1, 1, &target); // readPixels: y = 0 is the bottom
    const auto bottom = gl.device->readPixels(8, 3, 1, 1, &target);
    CHECK(top[0] > 150);
    CHECK(top[2] == 0);
    CHECK(bottom[2] > 150);
    CHECK(bottom[0] == 0);
    CHECK(gl.device->debugErrorCount() == 0);
}
