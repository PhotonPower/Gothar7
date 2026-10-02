// RHI end to end on a real OpenGL driver (label "gpu"): render into an offscreen framebuffer and
// read the pixels back.

#include "GlFixture.hpp"

#include <array>
#include <cstring>
#include <string>
#include <vector>

using namespace g7;
using namespace g7::render;
using namespace g7::render::rhi;
using g7::test::GlFixture;
using g7::test::require;

namespace
{
constexpr u32 kTargetSize = 16;

struct Vertex
{
    f32 x, y, z;
    u8 r, g, b, a;
};
static_assert(sizeof(Vertex) == 16);

constexpr std::string_view kColorVs = R"(#version 450 core
layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec4 aColor;
out vec4 vColor;
void main() { vColor = aColor; gl_Position = vec4(aPosition, 1.0); }
)";

constexpr std::string_view kColorFs = R"(#version 450 core
in vec4 vColor;
out vec4 fragColor;
void main() { fragColor = vColor; }
)";

template <typename T>
std::span<const u8> bytes(const std::vector<T>& values)
{
    return {reinterpret_cast<const u8*>(values.data()), values.size() * sizeof(T)};
}

/// Offscreen colour + depth target and a position/colour pipeline.
struct Scene
{
    GlFixture gl;
    Texture color;
    Texture depth;
    Framebuffer target;
    ShaderProgram program;

    Scene()
    {
        color = require(gl.device->createTexture({kTargetSize, kTargetSize, Format::RGBA8, 1}));
        depth = require(gl.device->createTexture({kTargetSize, kTargetSize, Format::Depth24Stencil8, 1}));
        target = require(gl.device->createFramebuffer({{&color}, &depth}));
        program = require(gl.device->createShaderProgram({kColorVs, kColorFs, "color"}));
    }

    Pipeline pipeline(BlendMode blend = BlendMode::Opaque, bool depthTest = true)
    {
        PipelineDesc desc;
        desc.program = &program;
        desc.attributes = {{0, VertexFormat::Float3, 0}, {1, VertexFormat::UNorm8x4, 12}};
        desc.vertexStride = sizeof(Vertex);
        desc.cull = CullMode::None;
        desc.depthTest = depthTest;
        desc.blend = blend;
        return require(gl.device->createPipeline(desc));
    }

    Buffer vertices(const std::vector<Vertex>& data)
    {
        return require(
            gl.device->createBuffer({data.size() * sizeof(Vertex), BufferUsage::Static, bytes(data)}));
    }

    void begin(const Vec4& clearColor = Vec4(0, 0, 0, 1))
    {
        gl.device->bindFramebuffer(&target);
        gl.device->setViewport(0, 0, kTargetSize, kTargetSize);
        gl.device->clear(clearColor, 0.0f); // reverse-Z: 0 = far
    }

    std::array<u8, 4> pixel(i32 x, i32 y)
    {
        const auto data = gl.device->readPixels(x, y, 1, 1, &target);
        REQUIRE(data.size() == 4);
        return {data[0], data[1], data[2], data[3]};
    }
};

/// Triangle covering the whole viewport at clip depth z (0..1, reverse-Z: 1 = near).
std::vector<Vertex> fullscreenTriangle(f32 z, u8 r, u8 g, u8 b, u8 a = 255)
{
    return {{-1, -1, z, r, g, b, a}, {3, -1, z, r, g, b, a}, {-1, 3, z, r, g, b, a}};
}

bool near(u8 actual, int expected, int tolerance = 2)
{
    return actual >= expected - tolerance && actual <= expected + tolerance;
}
} // namespace

TEST_CASE("RHI: buffer create, update, read back")
{
    GlFixture gl;
    const std::vector<u8> initial = {1, 2, 3, 4, 5, 6, 7, 8};
    Buffer buffer = require(gl.device->createBuffer({initial.size(), BufferUsage::Dynamic, initial}));
    CHECK(gl.device->readBuffer(buffer, 0, 8) == initial);

    const std::vector<u8> patch = {42, 43};
    REQUIRE(buffer.update(3, patch).ok());
    CHECK(gl.device->readBuffer(buffer, 2, 4) == std::vector<u8>{3, 42, 43, 6});

    CHECK_FALSE(buffer.update(7, patch).ok()); // out of range
    CHECK(gl.device->readBuffer(buffer, 7, 2).empty());

    Buffer fixed = require(gl.device->createBuffer({4, BufferUsage::Static, {}}));
    CHECK_FALSE(fixed.update(0, patch).ok());

    CHECK_FALSE(gl.device->createBuffer({0, BufferUsage::Static, {}}).ok());
    CHECK_FALSE(gl.device->createBuffer({16, BufferUsage::Static, initial}).ok()); // size mismatch
    CHECK(gl.device->debugErrorCount() == 0);
}

TEST_CASE("RHI: shader errors carry the compiler log")
{
    GlFixture gl;
    constexpr std::string_view broken =
        "#version 450 core\nout vec4 fragColor;\nvoid main() { fragColor = oops; }\n";
    auto program = gl.device->createShaderProgram({kColorVs, broken, "broken"});
    REQUIRE_FALSE(program.ok());
    const std::string& message = program.error().message;
    MESSAGE(message);
    CHECK(message.find("'broken' (fragment)") != std::string::npos);
    // Line 3, in the driver's own notation (Mesa "0:3(..)", Intel "0:3:", NVIDIA "0(3)").
    CHECK((message.find("0:3") != std::string::npos || message.find("0(3)") != std::string::npos));

    CHECK(gl.device->createShaderProgram({kColorVs, kColorFs, "ok"}).ok());
}

TEST_CASE("RHI: triangle into a framebuffer")
{
    Scene scene;
    Pipeline pipeline = scene.pipeline();
    Buffer triangle = scene.vertices(fullscreenTriangle(0.0f, 255, 0, 0));

    scene.gl.device->beginFrame(64, 64, Vec4(0.0f)); // resets stats
    scene.begin();
    scene.gl.device->bindPipeline(pipeline);
    scene.gl.device->bindVertexBuffer(triangle);
    scene.gl.device->draw(3);

    CHECK(scene.pixel(8, 8) == std::array<u8, 4>{255, 0, 0, 255});
    CHECK(scene.gl.device->stats().drawCalls == 1);
    CHECK(scene.gl.device->stats().triangles == 1);
    CHECK(scene.gl.device->debugErrorCount() == 0);
}

TEST_CASE("RHI: indexed quad")
{
    Scene scene;
    Pipeline pipeline = scene.pipeline();
    Buffer quad = scene.vertices({{-1, -1, 0, 0, 255, 0, 255},
                                  {1, -1, 0, 0, 255, 0, 255},
                                  {1, 1, 0, 0, 255, 0, 255},
                                  {-1, 1, 0, 0, 255, 0, 255}});
    const std::vector<u16> indices = {0, 1, 2, 0, 2, 3};
    Buffer indexBuffer = require(scene.gl.device->createBuffer({12, BufferUsage::Static, bytes(indices)}));

    scene.begin();
    scene.gl.device->bindPipeline(pipeline);
    scene.gl.device->bindVertexBuffer(quad);
    scene.gl.device->bindIndexBuffer(indexBuffer, IndexType::U16);
    scene.gl.device->drawIndexed(6);

    CHECK(scene.pixel(0, 0) == std::array<u8, 4>{0, 255, 0, 255});
    CHECK(scene.pixel(15, 15) == std::array<u8, 4>{0, 255, 0, 255});
    CHECK(scene.pixel(15, 0) == std::array<u8, 4>{0, 255, 0, 255});
    CHECK(scene.gl.device->debugErrorCount() == 0);
}

TEST_CASE("RHI: depth test keeps the nearer surface")
{
    Scene scene;
    Pipeline pipeline = scene.pipeline();
    // Reverse-Z: larger depth is nearer.
    Buffer nearBlue = scene.vertices(fullscreenTriangle(0.75f, 0, 0, 255));
    Buffer farRed = scene.vertices(fullscreenTriangle(0.25f, 255, 0, 0));

    scene.begin();
    scene.gl.device->bindPipeline(pipeline);
    scene.gl.device->bindVertexBuffer(nearBlue);
    scene.gl.device->draw(3);
    scene.gl.device->bindVertexBuffer(farRed); // drawn later but behind
    scene.gl.device->draw(3);

    CHECK(scene.pixel(8, 8) == std::array<u8, 4>{0, 0, 255, 255});
    CHECK(scene.gl.device->debugErrorCount() == 0);
}

TEST_CASE("RHI: alpha blending")
{
    Scene scene;
    Pipeline blended = scene.pipeline(BlendMode::Alpha, false);
    Buffer halfWhite = scene.vertices(fullscreenTriangle(0.0f, 255, 255, 255, 128));

    scene.begin(Vec4(0, 0, 0, 1));
    scene.gl.device->bindPipeline(blended);
    scene.gl.device->bindVertexBuffer(halfWhite);
    scene.gl.device->draw(3);

    const auto p = scene.pixel(8, 8);
    CHECK(near(p[0], 128));
    CHECK(near(p[1], 128));
    CHECK(near(p[2], 128));
    CHECK(scene.gl.device->debugErrorCount() == 0);
}

TEST_CASE("RHI: texture sampling and mipmaps")
{
    Scene scene;
    constexpr std::string_view vs = R"(#version 450 core
layout(location = 0) in vec3 aPosition;
out vec2 vUv;
void main() { vUv = aPosition.xy * 0.5 + 0.5; gl_Position = vec4(aPosition, 1.0); }
)";
    constexpr std::string_view fs = R"(#version 450 core
layout(binding = 0) uniform sampler2D uTexture;
in vec2 vUv;
out vec4 fragColor;
void main() { fragColor = texture(uTexture, vUv); }
)";
    ShaderProgram program = require(scene.gl.device->createShaderProgram({vs, fs, "textured"}));
    PipelineDesc desc;
    desc.program = &program;
    desc.attributes = {{0, VertexFormat::Float3, 0}};
    desc.vertexStride = sizeof(Vertex);
    desc.cull = CullMode::None;
    Pipeline pipeline = require(scene.gl.device->createPipeline(desc));
    Buffer triangle = scene.vertices(fullscreenTriangle(0.0f, 0, 0, 0));

    // 2x2 texels, first row is the bottom one (v = 0): red, green / blue, white.
    Texture texture = require(scene.gl.device->createTexture({2, 2, Format::RGBA8, 1}));
    const std::vector<u8> texels = {255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255, 255, 255, 255, 255};
    REQUIRE(texture.upload(0, texels).ok());
    CHECK_FALSE(texture.upload(0, std::vector<u8>(3)).ok()); // wrong size
    CHECK_FALSE(texture.upload(1, texels).ok());             // level does not exist

    SamplerDesc samplerDesc;
    samplerDesc.minFilter = samplerDesc.magFilter = samplerDesc.mipFilter = Filter::Nearest;
    samplerDesc.wrapU = samplerDesc.wrapV = Wrap::Clamp;
    samplerDesc.maxAnisotropy = 8.0f; // ignored with nearest filtering (would be driver-dependent)
    Sampler sampler = require(scene.gl.device->createSampler(samplerDesc));

    scene.begin();
    scene.gl.device->bindPipeline(pipeline);
    scene.gl.device->bindVertexBuffer(triangle);
    scene.gl.device->bindTexture(0, texture, sampler);
    scene.gl.device->draw(3);

    CHECK(scene.pixel(4, 4) == std::array<u8, 4>{255, 0, 0, 255});
    CHECK(scene.pixel(12, 4) == std::array<u8, 4>{0, 255, 0, 255});
    CHECK(scene.pixel(4, 12) == std::array<u8, 4>{0, 0, 255, 255});
    CHECK(scene.pixel(12, 12) == std::array<u8, 4>{255, 255, 255, 255});

    Texture mipped = require(scene.gl.device->createTexture({4, 4, Format::RGBA8, 0}));
    CHECK(mipped.desc().mipLevels == 3);
    REQUIRE(mipped.upload(0, std::vector<u8>(4 * 4 * 4, 200)).ok());
    REQUIRE(mipped.upload(2, std::vector<u8>(4, 10)).ok()); // 1x1 level
    mipped.generateMipmaps();
    CHECK(scene.gl.device->debugErrorCount() == 0);
}

TEST_CASE("RHI: invalid framebuffers are rejected")
{
    GlFixture gl;
    Texture color = require(gl.device->createTexture({8, 8, Format::RGBA8, 1}));
    Texture otherSize = require(gl.device->createTexture({4, 4, Format::RGBA8, 1}));
    Texture depth = require(gl.device->createTexture({8, 8, Format::Depth32F, 1}));

    CHECK_FALSE(gl.device->createFramebuffer({{}, nullptr}).ok());       // nothing attached: incomplete
    CHECK_FALSE(gl.device->createFramebuffer({{&depth}, nullptr}).ok()); // depth as colour
    CHECK_FALSE(gl.device->createFramebuffer({{&color}, &color}).ok());  // colour as depth
    CHECK_FALSE(gl.device->createFramebuffer({{&color, &otherSize}, nullptr}).ok());
    CHECK(gl.device->createFramebuffer({{}, &depth}).ok()); // depth-only (shadow map)
    CHECK(gl.device->createFramebuffer({{&color}, &depth}).ok());
    CHECK(gl.device->debugErrorCount() == 0);
}

TEST_CASE("RHI: redundant pipeline binds are skipped")
{
    Scene scene;
    Pipeline a = scene.pipeline();
    Pipeline b = scene.pipeline(BlendMode::Additive);
    scene.gl.device->beginFrame(64, 64, Vec4(0.0f));
    scene.gl.device->bindPipeline(a);
    scene.gl.device->bindPipeline(a);
    CHECK(scene.gl.device->stats().pipelineChanges == 1);
    scene.gl.device->bindPipeline(b);
    scene.gl.device->bindPipeline(a);
    CHECK(scene.gl.device->stats().pipelineChanges == 3);
    CHECK(scene.gl.device->debugErrorCount() == 0);
}

TEST_CASE("RHI: shader uniforms")
{
    Scene scene;
    constexpr std::string_view fs = R"(#version 450 core
uniform vec4 uTint;
out vec4 fragColor;
void main() { fragColor = uTint; }
)";
    ShaderProgram program = require(scene.gl.device->createShaderProgram({kColorVs, fs, "tint"}));
    program.setUniform("uTint", Vec4(0.0f, 1.0f, 1.0f, 1.0f));
    program.setUniform("uDoesNotExist", 1.0f); // ignored
    PipelineDesc desc;
    desc.program = &program;
    desc.attributes = {{0, VertexFormat::Float3, 0}};
    desc.vertexStride = sizeof(Vertex);
    desc.cull = CullMode::None;
    Pipeline pipeline = require(scene.gl.device->createPipeline(desc));
    Buffer triangle = scene.vertices(fullscreenTriangle(0.0f, 0, 0, 0));

    scene.begin();
    scene.gl.device->bindPipeline(pipeline);
    scene.gl.device->bindVertexBuffer(triangle);
    scene.gl.device->draw(3);
    CHECK(scene.pixel(8, 8) == std::array<u8, 4>{0, 255, 255, 255});
    CHECK(scene.gl.device->debugErrorCount() == 0);
}
