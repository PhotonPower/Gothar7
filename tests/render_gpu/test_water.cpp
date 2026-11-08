// Water surfaces on the GPU (M17 W1): the camera looks straight down onto a flat "floor" made by clearing the
// scene target (colour and depth), the water lies between.

#include "GlFixture.hpp"

#include <g7/render/Camera.hpp>
#include <g7/render/PostProcess.hpp>
#include <g7/render/ShaderLibrary.hpp>
#include <g7/render/Water.hpp>

#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>

using namespace g7;
using namespace g7::render;
using g7::test::GlFixture;
using g7::test::require;

namespace
{
constexpr u32 kSize = 64;
constexpr f32 kCameraHeight = 10.0f;

struct WaterScene
{
    GlFixture gl;
    ShaderLibrary library{*gl.device, fs::fromUtf8(G7_SHADER_DIR)};
    WaterRenderer water = require(WaterRenderer::create(*gl.device, library));
    SceneTarget scene = require(SceneTarget::create(*gl.device, kSize, kSize));
    Camera camera;
    WaterFrame frame;

    WaterScene()
    {
        camera.transform.position = Vec3(0.0f, kCameraHeight, 0.0f);
        camera.transform.rotation = glm::angleAxis(glm::radians(-90.0f), Vec3(1.0f, 0.0f, 0.0f)); // down
        camera.fovY = glm::radians(30.0f);
        camera.aspect = 1.0f;
        frame.sunColor = Vec3(0.0f); // no glint
        frame.ambient = Vec3(0.0f);  // black deep water
        frame.skyHorizon = frame.skyZenith = Vec3(0.0f);
    }
    /// Reverse-Z depth of a horizontal floor at height y (straight down, so the view depth is constant).
    f32 depthOf(f32 y) const
    {
        const Vec4 clip = camera.viewProjection() * Vec4(0.0f, y, 0.0f, 1.0f);
        return clip.z / clip.w;
    }
    /// The floor at `y` in `color` over the rectangle (all when empty).
    void floor(f32 y, const Vec4& color, std::optional<PixelRect> rect = std::nullopt)
    {
        gl.device->bindFramebuffer(&scene.framebuffer());
        gl.device->setViewport(0, 0, kSize, kSize);
        gl.device->setScissor(rect);
        gl.device->clear(color, std::nullopt);
        gl.device->setScissor(std::nullopt);
        if (rect)
        {
            gl.device->clearDepthRegion(scene.depth(), *rect, depthOf(y));
        }
        else
        {
            gl.device->clear(std::nullopt, depthOf(y));
        }
    }
    Vec3 pixel(u32 x, u32 y) const
    {
        const std::vector<f32> rgba = gl.device->readTextureFloat(scene.color(), 0);
        const usize i = (static_cast<usize>(y) * kSize + x) * 4;
        return Vec3(rgba[i], rgba[i + 1], rgba[i + 2]);
    }
    u32 draw(f32 surface, const WaterStyle& style)
    {
        const WaterSurface s{Vec3(0.0f, surface, 0.0f), 0.0f, Vec2(50.0f), &style};
        gl.device->bindFramebuffer(&scene.framebuffer());
        gl.device->setViewport(0, 0, kSize, kSize);
        return water.draw(*gl.device, scene, camera, std::span(&s, 1), frame);
    }
};

WaterStyle calm()
{
    WaterStyle style;
    style.waveStrength = 0.0f; // a flat mirror: no refraction offset
    style.absorption = Vec3(0.5f);
    style.scatter = Vec3(0.0f);
    return style;
}
} // namespace

TEST_CASE("Water surface: deeper water is darker, shallow water fades into the shore without a jump")
{
    WaterScene w;
    const WaterStyle style = calm();
    const Vec4 sand(0.8f, 0.6f, 0.4f, 1.0f);
    w.floor(0.0f, sand);
    REQUIRE(w.draw(1.0f, style) == 1); // 1 m of water
    const Vec3 shallow = w.pixel(32, 32);
    w.floor(0.0f, sand);
    w.draw(3.0f, style); // 3 m
    const Vec3 deep = w.pixel(32, 32);
    CHECK(shallow.x < sand.x);
    CHECK(deep.x < shallow.x);
    CHECK(shallow.x == doctest::Approx(sand.x * std::exp(-0.5f)).epsilon(0.05));
    // The shore: a hand's breadth of water hardly changes the floor.
    w.floor(0.0f, sand);
    w.draw(0.01f, style);
    const Vec3 shore = w.pixel(32, 32);
    CHECK(shore.x == doctest::Approx(sand.x).epsilon(0.06));
    CHECK(w.gl.device->debugErrorCount() == 0);
}

TEST_CASE("Water surface: under water the floor shifts with the waves; what stands above the water is not "
          "pulled in")
{
    WaterScene w;
    WaterStyle wavy = calm();
    wavy.waveStrength = 1.0f;
    wavy.refraction = 0.05f;
    wavy.absorption = Vec3(0.05f);
    wavy.shoreFade = 0.01f;
    const PixelRect right{kSize / 2, 0, kSize / 2, kSize};
    const auto blueNearEdge = [&](f32 rightFloor)
    {
        // Left red floor 2 m under the water, right a blue floor at `rightFloor`.
        w.floor(0.0f, Vec4(1.0f, 0.0f, 0.0f, 1.0f));
        w.floor(rightFloor, Vec4(0.0f, 0.0f, 1.0f, 1.0f), right);
        w.draw(2.0f, wavy);
        f32 blue = 0.0f;
        for (u32 y = 0; y < kSize; ++y)
        {
            for (u32 x = kSize / 2 - 4; x < kSize / 2; ++x)
            {
                blue = std::max(blue, w.pixel(x, y).z);
            }
        }
        return blue;
    };
    // The blue floor under water too: refraction carries some of it over the edge.
    CHECK(blueNearEdge(0.0f) > 0.1f);
    // The blue block reaching above the water: no water there, and its colour never leaks into the water.
    CHECK(blueNearEdge(5.0f) < 0.02f);
    CHECK(w.pixel(kSize - 4, 32).z == doctest::Approx(1.0f)); // the block itself stays as it was
    // Without waves nothing moves.
    wavy.waveStrength = 0.0f;
    CHECK(blueNearEdge(0.0f) < 0.02f);
    CHECK(w.gl.device->debugErrorCount() == 0);
}

TEST_CASE("Water surface: low quality draws without copies; nothing visible draws nothing")
{
    WaterScene w;
    const WaterStyle style = calm();
    w.frame.quality = WaterQuality::Low;
    w.frame.ambient = Vec3(1.0f);
    w.floor(0.0f, Vec4(1.0f, 1.0f, 1.0f, 1.0f));
    REQUIRE(w.draw(1.0f, style) == 1);
    CHECK(w.pixel(32, 32).x < 0.5f); // the dark surface over the white floor (scatter 0)
    // Water behind the camera: not drawn.
    w.camera.transform.position.y = -5.0f;
    w.camera.transform.rotation = glm::angleAxis(glm::radians(90.0f), Vec3(1.0f, 0.0f, 0.0f)); // looking up
    w.camera.transform.position = Vec3(500.0f, 20.0f, 0.0f);
    CHECK(w.draw(1.0f, style) == 0);
    CHECK(w.gl.device->debugErrorCount() == 0);
}

TEST_CASE("Water surface: high quality reflects what stands on the screen, the sky where the ray leaves it "
          "(M17 W2)")
{
    WaterScene w;
    // Looking slightly down over the water towards a "wall" filling the upper part of the screen 20 m ahead.
    w.camera.transform.position = Vec3(0.0f, 2.0f, 0.0f);
    w.camera.transform.rotation = glm::angleAxis(glm::radians(-15.0f), Vec3(1.0f, 0.0f, 0.0f));
    w.camera.fovY = glm::radians(60.0f);
    WaterStyle style = calm();
    style.absorption = Vec3(50.0f); // opaque water: only the reflection shows
    const auto wallDepth = [&]
    {
        const Vec4 clip = w.camera.projection() * Vec4(0.0f, 0.0f, -20.0f, 1.0f);
        return clip.z / clip.w;
    }();
    const auto render = [&](WaterQuality quality, bool wall)
    {
        w.frame.quality = quality;
        w.gl.device->bindFramebuffer(&w.scene.framebuffer());
        w.gl.device->setViewport(0, 0, kSize, kSize);
        w.gl.device->clear(Vec4(0.0f, 0.0f, 0.0f, 1.0f), 0.0f); // black sky, infinitely far
        if (wall)
        {
            const PixelRect upper{0, kSize / 2 + 4, kSize, kSize / 2 - 4};
            w.gl.device->setScissor(upper);
            w.gl.device->clear(Vec4(5.0f, 0.0f, 0.0f, 1.0f), std::nullopt);
            w.gl.device->setScissor(std::nullopt);
            w.gl.device->clearDepthRegion(w.scene.depth(), upper, wallDepth);
        }
        const WaterSurface s{Vec3(0.0f, 0.0f, -10.0f), 0.0f, Vec2(50.0f, 10.0f), &style};
        w.gl.device->bindFramebuffer(&w.scene.framebuffer());
        w.gl.device->setViewport(0, 0, kSize, kSize);
        REQUIRE(w.water.draw(*w.gl.device, w.scene, w.camera, std::span(&s, 1), w.frame) == 1);
        f32 red = 0.0f;
        for (u32 y = 0; y < kSize / 2; ++y) // the water below the horizon
        {
            red = std::max(red, w.pixel(kSize / 2, y).x);
        }
        return red;
    };
    CHECK(render(WaterQuality::High, true) > 0.05f);   // the red wall appears in the water
    CHECK(render(WaterQuality::Medium, true) < 0.01f); // medium: the (black) sky only
    CHECK(render(WaterQuality::High, false) < 0.01f);  // nothing on the screen to reflect: the sky
    CHECK(w.gl.device->debugErrorCount() == 0);
}

TEST_CASE("Water surface: rain rings ruffle a still surface (M17 R)")
{
    WaterScene w;
    const WaterStyle style = calm();
    w.frame.quality = WaterQuality::Low;
    w.frame.skyHorizon = w.frame.skyZenith = Vec3(1.0f); // the reflection shows the tilt
    w.frame.time = 1.234f;
    // A grazing view: there the reflection reacts strongly to the tilt.
    w.camera.transform.position = Vec3(0.0f, 2.0f, 0.0f);
    w.camera.transform.rotation = glm::angleAxis(glm::radians(-20.0f), Vec3(1.0f, 0.0f, 0.0f));
    w.camera.fovY = glm::radians(40.0f);
    const auto spread = [&](f32 rain)
    {
        w.frame.rain = rain;
        w.gl.device->bindFramebuffer(&w.scene.framebuffer());
        w.gl.device->setViewport(0, 0, kSize, kSize);
        w.gl.device->clear(Vec4(0.0f, 0.0f, 0.0f, 1.0f), 0.0f); // nothing behind the water
        REQUIRE(w.draw(1.0f, style) == 1);
        const std::vector<f32> rgba = w.gl.device->readTextureFloat(w.scene.color(), 0);
        // Ripples: the colour jumps between neighbouring pixels (a flat surface changes only smoothly).
        f32 roughness = 0.0f;
        for (usize i = 4; i < rgba.size() / 3; i += 4) // the lower part of the image: the water
        {
            roughness += std::abs(rgba[i] - rgba[i - 4]);
        }
        return roughness;
    };
    const f32 still = spread(0.0f);
    CHECK(spread(1.0f) > 3.0f * still + 0.05f); // rings
    CHECK(w.gl.device->debugErrorCount() == 0);
}
