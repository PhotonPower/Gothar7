// Needs a real OpenGL 4.5+ driver (label "gpu"): a GPU locally, Mesa llvmpipe under Xvfb in Linux CI.

#include <g7/platform/GlContext.hpp>
#include <g7/platform/Window.hpp>
#include <g7/render/Device.hpp>

#include <doctest/doctest.h>

#include <memory>

using namespace g7;

namespace
{
struct GlFixture
{
    std::unique_ptr<platform::Window> window;
    std::unique_ptr<platform::GlContext> context;
    std::unique_ptr<render::Device> device;

    GlFixture()
    {
        platform::WindowDesc desc;
        desc.title = "g7 render test";
        desc.size = {64, 64};
        desc.graphics = platform::GraphicsApi::OpenGL;
        auto w = platform::Window::create(desc);
        REQUIRE_MESSAGE(w.ok(), (w.ok() ? "" : w.error().message));
        window = std::move(w).value();

        platform::GlContextDesc contextDesc;
        contextDesc.debug = true;
        auto c = platform::GlContext::create(*window, contextDesc);
        REQUIRE_MESSAGE(c.ok(), (c.ok() ? "" : c.error().message));
        context = std::move(c).value();

        auto d = render::Device::create(&platform::GlContext::procAddress, true);
        REQUIRE_MESSAGE(d.ok(), (d.ok() ? "" : d.error().message));
        device = std::move(d).value();
    }

    ~GlFixture()
    {
        device.reset(); // before the context
        context.reset();
    }
};
} // namespace

TEST_CASE("GL: context and device report at least OpenGL 4.5")
{
    GlFixture gl;
    const auto& info = gl.device->info();
    MESSAGE("OpenGL ", info.version, " - ", info.renderer);
    CHECK(gl.context->major() == 4);
    CHECK(gl.context->minor() >= 5);
    CHECK((info.major > 4 || (info.major == 4 && info.minor >= 5)));
    CHECK_FALSE(info.renderer.empty());
}

TEST_CASE("GL: cleared colour can be read back")
{
    GlFixture gl;
    const auto size = gl.window->pixelSize();
    gl.device->beginFrame(size.width, size.height, Vec4(1.0f, 0.5f, 0.0f, 1.0f));
    const auto pixels = gl.device->readPixels(4, 4, 2, 2);
    REQUIRE(pixels.size() == 16);
    for (usize i = 0; i < pixels.size(); i += 4)
    {
        CHECK(pixels[i + 0] == 255);
        CHECK(pixels[i + 1] >= 127);
        CHECK(pixels[i + 1] <= 128);
        CHECK(pixels[i + 2] == 0);
        CHECK(pixels[i + 3] == 255);
    }
    CHECK(gl.device->debugErrorCount() == 0);
    gl.context->swapBuffers();
}

TEST_CASE("GL: errors reach the debug callback")
{
    GlFixture gl;
    CHECK(gl.device->debugErrorCount() == 0);
    (void)gl.device->readPixels(0, 0, 1, 1); // valid, no error
    CHECK(gl.device->debugErrorCount() == 0);
    // A viewport width that wraps to a negative GLsizei is GL_INVALID_VALUE.
    gl.device->beginFrame(0xFFFFFFFFu, 1, Vec4(0.0f));
    CHECK(gl.device->debugErrorCount() >= 1);
}
