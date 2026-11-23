// Rain on the GPU (M17 weather part R): streaks show in the open and vanish under a roof (the rain map); no
// rain, nothing drawn.

#include "GlFixture.hpp"

#include <g7/render/Camera.hpp>
#include <g7/render/PostProcess.hpp>
#include <g7/render/Rain.hpp>
#include <g7/render/ShaderLibrary.hpp>

#include <doctest/doctest.h>

using namespace g7;
using namespace g7::render;
using g7::test::GlFixture;
using g7::test::require;

TEST_CASE("Rain surface: streaks in the open, none under a roof, none without rain")
{
    GlFixture gl;
    ShaderLibrary library(*gl.device, fs::fromUtf8(G7_SHADER_DIR));
    RainRenderer rain = require(RainRenderer::create(*gl.device, library, 64));
    constexpr u32 kSize = 96;
    SceneTarget scene = require(SceneTarget::create(*gl.device, kSize, kSize));
    Camera camera;
    camera.transform.position = Vec3(0.0f, 1.7f, 0.0f);
    camera.aspect = 1.0f;
    RainFrame frame;
    frame.rain = 1.0f;
    frame.time = 3.0f;
    frame.light = Vec3(1.0f);
    frame.quality = RainQuality::High;
    const auto brightness = [&](bool roof, f32 amount)
    {
        // The map: the open sky (depth 1) or a roof 3 m above the camera everywhere.
        rain.beginMap(*gl.device, camera.transform.position);
        if (roof)
        {
            const Vec4 c =
                rain.mapViewProjection() * Vec4(camera.transform.position + Vec3(0.0f, 3.0f, 0.0f), 1.0f);
            gl.device->clear(std::nullopt, c.z / c.w);
        }
        rain.endMap();
        gl.device->bindFramebuffer(&scene.framebuffer());
        gl.device->setViewport(0, 0, kSize, kSize);
        gl.device->clear(Vec4(0.0f, 0.0f, 0.0f, 1.0f), 0.0f);
        frame.rain = amount;
        rain.draw(*gl.device, camera, frame);
        // The lower half of the image: drops below the camera's height - under a roof 3 m up they are all
        // hidden.
        const std::vector<f32> rgba = gl.device->readTextureFloat(scene.color(), 0);
        f32 sum = 0.0f;
        for (usize i = 0; i < static_cast<usize>(kSize) * (kSize / 2); ++i) // rows from the bottom
        {
            sum += rgba[i * 4] + rgba[i * 4 + 1] + rgba[i * 4 + 2];
        }
        return sum;
    };
    const f32 open = brightness(false, 1.0f);
    CHECK(open > 1.0f);
    CHECK(brightness(true, 1.0f) < open * 0.05f); // under a roof
    CHECK(brightness(false, 0.0f) == doctest::Approx(0.0f));
    CHECK(gl.device->debugErrorCount() == 0);
}
