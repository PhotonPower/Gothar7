#pragma once

// Window + GL context + device for GPU tests (label "gpu").

#include <g7/platform/GlContext.hpp>
#include <g7/platform/Window.hpp>
#include <g7/render/Device.hpp>

#include <doctest/doctest.h>

#include <chrono>
#include <memory>
#include <thread>

namespace g7::test
{
/// Creates a window, retrying briefly: under Xvfb (Linux CI) connecting to the X server
/// occasionally fails with "No available video device".
inline Result<std::unique_ptr<platform::Window>> createWindowWithRetry(const platform::WindowDesc& desc)
{
    auto window = platform::Window::create(desc);
    for (int attempt = 1; !window && attempt < 4; ++attempt)
    {
        MESSAGE("window creation failed (", window.error().message, "), retrying");
        std::this_thread::sleep_for(std::chrono::milliseconds(200 * attempt));
        window = platform::Window::create(desc);
    }
    return window;
}

/// Keeps SDL's video subsystem (and its X connection) alive for the whole test run, so the
/// many windows of the suite (fixtures, engines) do not each set it up and tear it down again.
/// Call before creating a window or an Engine.
inline void keepVideoAlive()
{
    static std::unique_ptr<platform::Window> keepAlive = []
    {
        platform::WindowDesc desc;
        desc.title = "g7 test video keep-alive";
        desc.size = {16, 16};
        desc.resizable = false;
        auto window = createWindowWithRetry(desc);
        REQUIRE_MESSAGE(window.ok(), (window.ok() ? "" : window.error().message));
        return std::move(window).value();
    }();
}

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
        keepVideoAlive();
        auto w = createWindowWithRetry(desc);
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

template <typename T>
T require(Result<T> result)
{
    REQUIRE_MESSAGE(result.ok(), (result.ok() ? "" : result.error().message));
    return std::move(result).value();
}
} // namespace g7::test
