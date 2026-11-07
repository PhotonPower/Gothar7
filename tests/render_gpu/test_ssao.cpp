// Ambient occlusion on the GPU (M17 P3): a step in the depth (a near block in front of a far wall) darkens
// the far wall next to it; an open flat wall stays open.

#include "GlFixture.hpp"

#include <g7/render/Camera.hpp>
#include <g7/render/PostProcess.hpp>
#include <g7/render/ShaderLibrary.hpp>
#include <g7/render/Ssao.hpp>

#include <doctest/doctest.h>

using namespace g7;
using namespace g7::render;
using g7::test::GlFixture;
using g7::test::require;

TEST_CASE("SSAO: the kernel lies in the hemisphere, denser near the centre")
{
    const std::vector<Vec3> kernel = ssaoKernel(kSsaoSamples);
    REQUIRE(kernel.size() == kSsaoSamples);
    for (const Vec3& k : kernel)
    {
        CHECK(k.z > 0.0f);
        CHECK(glm::length(k) <= 1.0f);
    }
    CHECK(glm::length(kernel.front()) < glm::length(kernel.back()));
}

TEST_CASE("SSAO surface: a corner darkens, an open wall stays open")
{
    GlFixture gl;
    ShaderLibrary library(*gl.device, fs::fromUtf8(G7_SHADER_DIR));
    Ssao ssao = require(Ssao::create(*gl.device, library));
    constexpr u32 kSize = 64;
    SceneTarget scene = require(SceneTarget::create(*gl.device, kSize, kSize));
    Camera camera;
    camera.aspect = 1.0f;
    camera.fovY = glm::radians(60.0f);
    const auto depthAt = [&](f32 metres)
    {
        const Vec4 clip = camera.projection() * Vec4(0.0f, 0.0f, -metres, 1.0f);
        return clip.z / clip.w;
    };
    const auto aoAt = [&](u32 x, u32 y)
    {
        const std::vector<f32> ao = gl.device->readTextureFloat(*ssao.result(), 0);
        const u32 w = ssao.result()->desc().width;
        REQUIRE(ao.size() >= (static_cast<usize>(y) * w + x + 1) * 4);
        return ao[(static_cast<usize>(y) * w + x) * 4];
    };
    gl.device->bindFramebuffer(&scene.framebuffer());
    gl.device->setViewport(0, 0, kSize, kSize);
    gl.device->clear(Vec4(0.0f), depthAt(5.0f)); // a wall 5 m away
    REQUIRE(ssao.resize(*gl.device, kSize, kSize).ok());
    ssao.render(*gl.device, scene.depth(), camera, {});
    REQUIRE(ssao.result() != nullptr);
    const f32 open = aoAt(16, 16);
    CHECK(open > 0.9f);
    // A block 0.3 m in front of the wall over the left half: the wall right next to it is in its shade.
    gl.device->bindFramebuffer(&scene.framebuffer());
    gl.device->clearDepthRegion(scene.depth(), {0, 0, kSize / 2, kSize}, depthAt(4.7f));
    ssao.render(*gl.device, scene.depth(), camera, {});
    const f32 corner = aoAt(kSize / 4 + 1, 16); // half resolution: just right of the block's edge
    const f32 far = aoAt(kSize / 2 - 2, 16);    // right edge of the image, away from the block
    CHECK(corner < far - 0.05f);
    CHECK(gl.device->debugErrorCount() == 0);
}
