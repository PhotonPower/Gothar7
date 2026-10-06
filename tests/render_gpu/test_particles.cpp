// Particles on a real driver (label "gpu", M12 part A): instanced quads with the procedural sprites, additive
// and alpha blending, hidden behind nearer geometry (depth-tested, not written).

#include "GlFixture.hpp"

#include <g7/render/Camera.hpp>
#include <g7/render/Particles.hpp>
#include <g7/render/ShaderLibrary.hpp>

using namespace g7;
using namespace g7::render;
using namespace g7::render::rhi;
using g7::test::GlFixture;
using g7::test::require;

namespace
{
constexpr u32 kSize = 64;

struct Scene
{
    GlFixture gl;
    ShaderLibrary library{*gl.device, fs::fromUtf8(G7_SHADER_DIR)};
    ParticleRenderer renderer;
    Texture color;
    Texture depth;
    Framebuffer target;
    Camera camera;

    Scene()
    {
        renderer = require(ParticleRenderer::create(*gl.device, library));
        color = require(gl.device->createTexture({kSize, kSize, Format::RGBA8, 1}));
        depth = require(gl.device->createTexture({kSize, kSize, Format::Depth32F, 1}));
        target = require(gl.device->createFramebuffer({{&color}, &depth}));
        camera.aspect = 1.0f;
        camera.transform.position = Vec3(0, 0, 5);
    }

    /// `sceneDepth`: window depth of a wall filling the view (0 = nothing, reverse-Z).
    std::vector<u8> render(std::span<const ParticleInstance> particles, bool additive, f32 sceneDepth = 0.0f)
    {
        gl.device->bindFramebuffer(&target);
        gl.device->setViewport(0, 0, kSize, kSize);
        gl.device->clear(Vec4(0, 0, 0, 1), sceneDepth);
        renderer.draw(*gl.device, camera, particles, additive);
        return gl.device->readPixels(0, 0, kSize, kSize, &target);
    }
};

u8 red(const std::vector<u8>& p, u32 x, u32 y)
{
    return p[(y * kSize + x) * 4];
}
} // namespace

TEST_CASE("Particles GPU: a glowing dot in the middle, nothing in the corner; additive adds up")
{
    Scene s;
    const ParticleInstance one{Vec3(0.0f), 2.0f, Vec4(1.0f, 0.5f, 0.2f, 1.0f), Vec3(0.0f), 0.0f};
    const auto single = s.render(std::span(&one, 1), true);
    CHECK(red(single, 32, 32) > 150);
    CHECK(red(single, 1, 1) == 0);
    // Two at the same place: brighter (additive).
    const ParticleInstance faint{Vec3(0.0f), 2.0f, Vec4(0.3f, 0.0f, 0.0f, 1.0f), Vec3(0.0f), 0.0f};
    const ParticleInstance two[] = {faint, faint};
    const u8 a = red(s.render(std::span(&faint, 1), true), 32, 32);
    const u8 b = red(s.render(two, true), 32, 32);
    CHECK(b > a);
    CHECK(s.gl.device->debugErrorCount() == 0);
}

TEST_CASE("Particles GPU: hidden behind a nearer wall; alpha smoke and a spark draw too")
{
    Scene s;
    const ParticleInstance one{Vec3(0.0f), 2.0f, Vec4(1.0f), Vec3(0.0f), 0.0f};
    CHECK(red(s.render(std::span(&one, 1), true, 0.9f), 32, 32) == 0); // a wall right in front (reverse-Z)
    const ParticleInstance smoke{Vec3(0.0f), 3.0f, Vec4(0.8f, 0.8f, 0.8f, 1.0f), Vec3(0.0f), 1.0f};
    const auto blotted = s.render(std::span(&smoke, 1), false);
    int sum = 0;
    for (u32 y = 20; y < 44; ++y)
    {
        for (u32 x = 20; x < 44; ++x)
        {
            sum += red(blotted, x, y);
        }
    }
    CHECK(sum > 0);
    const ParticleInstance spark{Vec3(0.0f), 1.0f, Vec4(1.0f), Vec3(0.0f, 5.0f, 0.0f), 2.0f};
    const auto streak = s.render(std::span(&spark, 1), true);
    CHECK(red(streak, 32, 40) > 0);  // long along the motion (up)
    CHECK(red(streak, 40, 32) == 0); // thin across it
    CHECK(s.gl.device->debugErrorCount() == 0);
}
