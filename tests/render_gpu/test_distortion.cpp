// Spell distortion on the GPU (M17 part V, label "gpu"): an empty offset buffer leaves the picture as it is;
// a shock ring shifts pixels only within its radius; distorting particles behind a wall write nothing; the
// wobble moves the whole picture.

#include "GlFixture.hpp"

#include <g7/asset/Procedural.hpp>
#include <g7/core/FileSystem.hpp>
#include <g7/render/Camera.hpp>
#include <g7/render/Distortion.hpp>
#include <g7/render/Lighting.hpp>
#include <g7/render/Material.hpp>
#include <g7/render/Mesh.hpp>
#include <g7/render/Particles.hpp>
#include <g7/render/PostProcess.hpp>
#include <g7/render/ShaderLibrary.hpp>

#include <vector>

using namespace g7;
using namespace g7::render;
using namespace g7::render::rhi;
using g7::test::GlFixture;
using g7::test::require;

namespace
{
constexpr u32 kSize = 256; // offsets are fractions of the picture: about a pixel here

ParticleInstance ring(const Vec3& at, f32 size, f32 age)
{
    ParticleInstance p;
    p.position = at;
    p.size = size;
    p.color = Vec4(1.0f, age, static_cast<f32>(DistortShape::Ring), 1.0f);
    return p;
}
} // namespace

TEST_CASE("Distortion GPU: none changes nothing, a ring shifts only within its radius, walls hide it")
{
    GlFixture gl;
    ShaderLibrary library(*gl.device, fs::fromUtf8(G7_SHADER_DIR));
    PostProcess post = require(PostProcess::create(*gl.device, library));
    MeshRenderer meshes = require(MeshRenderer::create(*gl.device, library, 1.0f));
    ParticleRenderer particles = require(ParticleRenderer::create(*gl.device, library));
    SceneTarget scene = require(SceneTarget::create(*gl.device, kSize, kSize));
    Texture output = require(gl.device->createTexture({kSize, kSize, Format::RGBA8, 1}));
    Framebuffer window = require(gl.device->createFramebuffer({{&output}, nullptr}));
    DistortionTarget distortion;

    // A white wall over the left half (x < 0) at z = 0, black far away elsewhere.
    asset::MeshData wallData = asset::makeBox(Vec3(1.5f, 1.5f, 0.05f), Vec4(1.0f));
    const Mesh wall = require(Mesh::create(*gl.device, wallData));
    const MaterialSet wallMaterials =
        require(MaterialSet::create(*gl.device, wallData, {}, meshes.defaults()));
    Camera camera;
    camera.aspect = 1.0f;
    camera.transform.position = Vec3(0.0f, 0.0f, 3.0f);
    const LightList none;
    const auto drawScene = [&]
    {
        gl.device->bindFramebuffer(&scene.framebuffer());
        gl.device->setViewport(0, 0, kSize, kSize);
        gl.device->clear(Vec4(0, 0, 0, 1), 0.0f);
        meshes.setLighting(
            *gl.device,
            Environment{.sunIntensity = 0.0f, .ambientSky = Vec3(1.0f), .ambientGround = Vec3(1.0f)}, none);
        meshes.draw(*gl.device, wall, wallMaterials, glm::translate(Mat4(1.0f), Vec3(-1.5f, 0.0f, 0.0f)),
                    camera);
    };
    const auto present = [&](const PostSettings& settings)
    {
        gl.device->bindFramebuffer(&window);
        post.apply(*gl.device, scene, kSize, kSize, settings);
        return gl.device->readPixels(0, 0, kSize, kSize, &window);
    };
    const auto redAt = [&](const std::vector<u8>& pixels, u32 x, u32 y)
    { return pixels[(static_cast<usize>(y) * kSize + x) * 4]; };

    drawScene();
    const PostSettings plain{Tonemapper::None, 1.0f};
    const auto before = present(plain);
    REQUIRE(redAt(before, 32, 128) > 200); // the wall
    REQUIRE(redAt(before, 224, 128) < 10); // the dark

    // An empty buffer: the same picture.
    REQUIRE(distortion.begin(*gl.device, scene).ok());
    PostSettings distorted = plain;
    distorted.distortion = &distortion.texture();
    CHECK(present(distorted) == before);

    // A ring beside the wall's edge, half grown: the edge moves where the ring crosses it, nowhere else.
    REQUIRE(distortion.begin(*gl.device, scene).ok());
    const std::vector<ParticleInstance> one{ring(Vec3(0.35f, 0.0f, 0.5f), 1.6f, 0.5f)};
    particles.drawDistortion(*gl.device, camera, one);
    const auto after = present(distorted);
    usize changed = 0;
    usize changedFar = 0;
    for (u32 y = 0; y < kSize; ++y)
    {
        for (u32 x = 0; x < kSize; ++x)
        {
            if (redAt(after, x, y) != redAt(before, x, y))
            {
                ++changed;
                const f32 dx = static_cast<f32>(x) - 128.0f;
                const f32 dy = static_cast<f32>(y) - 128.0f;
                changedFar += dx * dx + dy * dy > 80.0f * 80.0f ? 1 : 0;
            }
        }
    }
    CHECK(changed > 4);
    CHECK(changedFar == 0);

    // Behind the wall: nothing written where the wall is (depth test), offsets beside it.
    REQUIRE(distortion.begin(*gl.device, scene).ok());
    const std::vector<ParticleInstance> behind{ring(Vec3(0.0f, 0.0f, -2.0f), 4.0f, 0.8f)};
    particles.drawDistortion(*gl.device, camera, behind);
    const auto offsets = gl.device->readTextureFloat(distortion.texture(), 0);
    f32 onWall = 0.0f;
    f32 beside = 0.0f;
    for (u32 y = 0; y < kSize; ++y)
    {
        for (u32 x = 0; x < kSize; ++x)
        {
            const usize i = (static_cast<usize>(y) * kSize + x) * 4;
            const f32 length = std::abs(offsets[i]) + std::abs(offsets[i + 1]);
            if (x < 96)
            {
                onWall += length;
            }
            else if (x > 160)
            {
                beside += length;
            }
        }
    }
    CHECK(onWall == 0.0f);
    CHECK(beside > 0.0f);

    // The wobble (the hero changes shape) moves the picture without any buffer.
    PostSettings wobbling = plain;
    wobbling.wobble = 1.0f;
    wobbling.wobbleTime = 0.3f;
    CHECK(present(wobbling) != before);
    CHECK(gl.device->debugErrorCount() == 0);
}
