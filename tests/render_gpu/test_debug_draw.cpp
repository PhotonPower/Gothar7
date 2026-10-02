// Debug drawing on a real driver (label "gpu"): lines, depth test against the scene, text.

#include "GlFixture.hpp"

#include <g7/render/Camera.hpp>
#include <g7/render/DebugDraw.hpp>
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
    DebugDrawRenderer renderer;
    Texture color;
    Texture depth;
    Framebuffer target;
    Camera camera;

    Scene()
    {
        renderer = require(DebugDrawRenderer::create(*gl.device, library));
        color = require(gl.device->createTexture({kSize, kSize, Format::RGBA8, 1}));
        depth = require(gl.device->createTexture({kSize, kSize, Format::Depth32F, 1}));
        target = require(gl.device->createFramebuffer({{&color}, &depth}));
        camera.aspect = 1.0f;
        camera.transform.position = Vec3(0, 0, 5);
    }

    /// `sceneDepth`: window depth of a "wall" filling the view (0 = nothing, reverse-Z).
    void render(const DebugDraw& debug, f32 sceneDepth = 0.0f, bool withDepth = true)
    {
        gl.device->bindFramebuffer(&target);
        gl.device->setViewport(0, 0, kSize, kSize);
        gl.device->clear(Vec4(0, 0, 0, 1), sceneDepth);
        renderer.render(*gl.device, debug, camera, withDepth ? &depth : nullptr, kSize, kSize);
    }

    std::vector<u8> pixels() { return gl.device->readPixels(0, 0, kSize, kSize, &target); }

    /// Sum of the red channel over a row (bottom-up index).
    int redInRow(u32 row)
    {
        const auto p = pixels();
        int sum = 0;
        for (u32 x = 0; x < kSize; ++x)
        {
            sum += p[(row * kSize + x) * 4];
        }
        return sum;
    }

    /// Window depth of a point `distance` in front of the camera.
    f32 depthAt(f32 distance)
    {
        const Vec4 clip =
            camera.viewProjection() * Vec4(camera.transform.position + Vec3(0, 0, -distance), 1.0f);
        return clip.z / clip.w;
    }
};

} // namespace

TEST_CASE("DebugDraw GPU: a line appears where it projects")
{
    Scene scene;
    DebugDraw debug;
    debug.line(Vec3(-10, 0, 0), Vec3(10, 0, 0), {Vec4(1, 0, 0, 1)});
    scene.render(debug);
    // Horizontal through the view centre: rows 31/32 hold it, rows far away stay black.
    CHECK(scene.redInRow(31) + scene.redInRow(32) > 255 * 50);
    CHECK(scene.redInRow(10) == 0);
    CHECK(scene.redInRow(55) == 0);
    CHECK(scene.gl.device->debugErrorCount() == 0);
}

TEST_CASE("DebugDraw GPU: hidden lines are faint unless depth testing is off")
{
    Scene scene;
    DebugDraw debug;
    debug.line(Vec3(-10, 0, 0), Vec3(10, 0, 0), {Vec4(1, 0, 0, 1)}); // 5 m in front of the camera
    const int visible = [&]
    {
        scene.render(debug);
        return scene.redInRow(31) + scene.redInRow(32);
    }();

    const f32 wall = scene.depthAt(2.0f); // a wall at 2 m hides the line
    scene.render(debug, wall);
    const int hidden = scene.redInRow(31) + scene.redInRow(32);
    CHECK(hidden > 0);           // still visible, ...
    CHECK(hidden < visible / 3); // ... but dashed and faint

    scene.render(debug, scene.depthAt(8.0f)); // wall behind the line: full
    CHECK(scene.redInRow(31) + scene.redInRow(32) == visible);

    DebugDraw always;
    always.line(Vec3(-10, 0, 0), Vec3(10, 0, 0), {Vec4(1, 0, 0, 1), 0.0f, false});
    scene.render(always, wall);
    CHECK(scene.redInRow(31) + scene.redInRow(32) == visible);

    scene.render(debug, wall, false); // no scene depth given: nothing counts as hidden
    CHECK(scene.redInRow(31) + scene.redInRow(32) == visible);
    CHECK(scene.gl.device->debugErrorCount() == 0);
}

TEST_CASE("DebugDraw GPU: text")
{
    Scene scene;
    DebugDraw debug;
    debug.screenText(Vec2(2, 2), "Gothar\n0123", Vec4(1, 1, 0, 1));
    debug.text(Vec3(0, -1, 0), "Hi?", {Vec4(0, 1, 1, 1)});
    scene.render(debug);
    const auto p = scene.pixels();
    int yellow = 0;
    int cyan = 0;
    for (usize i = 0; i < p.size(); i += 4)
    {
        yellow += (p[i] > 200 && p[i + 1] > 200 && p[i + 2] < 50) ? 1 : 0;
        cyan += (p[i] < 50 && p[i + 1] > 200 && p[i + 2] > 200) ? 1 : 0;
    }
    CHECK(yellow > 40);
    CHECK(cyan > 15);
    // Screen text sits at the top-left: the top rows (bottom-up indices 54..61) hold it, the
    // bottom-left corner does not.
    CHECK(p[((kSize - 6) * kSize + 3) * 4 + 3] == 255);
    int bottomLeft = 0;
    for (u32 y = 0; y < 8; ++y)
    {
        for (u32 x = 0; x < 8; ++x)
        {
            bottomLeft += p[(y * kSize + x) * 4];
        }
    }
    CHECK(bottomLeft == 0);
    CHECK(scene.gl.device->debugErrorCount() == 0);
}

TEST_CASE("DebugDraw GPU: an empty or disabled debug draw changes nothing")
{
    Scene scene;
    DebugDraw debug;
    scene.render(debug);
    debug.line(Vec3(-10, 0, 0), Vec3(10, 0, 0), {Vec4(1, 0, 0, 1)});
    debug.enabled = false;
    scene.render(debug);
    const auto p = scene.pixels();
    int sum = 0;
    for (usize i = 0; i < p.size(); i += 4)
    {
        sum += p[i] + p[i + 1] + p[i + 2];
    }
    CHECK(sum == 0);
    CHECK(scene.gl.device->debugErrorCount() == 0);
}
