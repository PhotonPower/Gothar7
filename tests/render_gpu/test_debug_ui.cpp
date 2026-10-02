// RHI scissor test and the ImGui debug UI drawn through the RHI (label "gpu").

#include "GlFixture.hpp"

#include <g7/platform/Input.hpp>
#include <g7/render/ShaderLibrary.hpp>
#include <g7/ui/DebugUi.hpp>

using namespace g7;
using namespace g7::render;
using namespace g7::render::rhi;
using g7::test::GlFixture;
using g7::test::require;

TEST_CASE("RHI: the scissor test limits clears and draws")
{
    GlFixture gl;
    Texture color = require(gl.device->createTexture({16, 16, Format::RGBA8, 1}));
    Framebuffer target = require(gl.device->createFramebuffer({{&color}, nullptr}));
    gl.device->bindFramebuffer(&target);
    gl.device->setViewport(0, 0, 16, 16);
    gl.device->clear(Vec4(0, 0, 0, 1), std::nullopt);

    gl.device->setScissor(PixelRect{4, 2, 8, 6}); // x 4..11, y 2..7 (bottom-up)
    gl.device->clear(Vec4(1, 0, 0, 1), std::nullopt);
    gl.device->setScissor(std::nullopt);
    const auto pixels = gl.device->readPixels(0, 0, 16, 16, &target);
    const auto red = [&](int x, int y) { return pixels[static_cast<usize>((y * 16 + x) * 4)]; };
    CHECK(red(4, 2) == 255);
    CHECK(red(11, 7) == 255);
    CHECK(red(3, 2) == 0);
    CHECK(red(12, 7) == 0);
    CHECK(red(4, 1) == 0);
    CHECK(red(4, 8) == 0);

    // Off again: a clear reaches everything.
    gl.device->clear(Vec4(0, 1, 0, 1), std::nullopt);
    CHECK(gl.device->readPixels(0, 0, 1, 1, &target)[1] == 255);
    CHECK(gl.device->debugErrorCount() == 0);
}

TEST_CASE("DebugUi: the engine panel renders through the RHI")
{
    GlFixture gl;
    ShaderLibrary library(*gl.device, fs::fromUtf8(G7_SHADER_DIR));
    ui::DebugUi ui = require(ui::DebugUi::create(gl.device.get(), &library, 1.0f));
    constexpr u32 kWidth = 480;
    constexpr u32 kHeight = 360;
    Texture color = require(gl.device->createTexture({kWidth, kHeight, Format::RGBA8, 1}));
    Framebuffer target = require(gl.device->createFramebuffer({{&color}, nullptr}));

    platform::Input input;
    const Vec2 size(static_cast<f32>(kWidth), static_cast<f32>(kHeight));
    for (int i = 0; i < 3; ++i) // the first frame builds and uploads the font atlas
    {
        gl.device->bindFramebuffer(&target);
        gl.device->setViewport(0, 0, kWidth, kHeight);
        gl.device->clear(Vec4(0, 0, 0, 1), std::nullopt);
        ui.beginFrame(input, size, size, 1.0f / 60.0f);
        ui::EnginePanel panel;
        panel.frame.drawCalls = 42;
        ui.enginePanel(panel);
        ui.endFrame(gl.device.get());
    }
    const auto pixels = gl.device->readPixels(0, 0, kWidth, kHeight, &target);
    // The panel sits at the top left: many lit pixels there, none at the bottom right.
    const auto lit = [&](u32 x0, u32 y0, u32 x1, u32 y1)
    {
        int count = 0;
        for (u32 y = y0; y < y1; ++y)
        {
            for (u32 x = x0; x < x1; ++x)
            {
                const usize i = (static_cast<usize>(y) * kWidth + x) * 4;
                count += (pixels[i] + pixels[i + 1] + pixels[i + 2]) > 60 ? 1 : 0;
            }
        }
        return count;
    };
    CHECK(lit(20, kHeight - 120, 200, kHeight - 20) > 500); // rows are bottom-up
    CHECK(lit(kWidth - 60, 0, kWidth, 60) == 0);
    CHECK(gl.device->debugErrorCount() == 0);
}
