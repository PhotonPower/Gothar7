#pragma once

// Window + GL context + device for GPU tests (label "gpu").

#include <g7/platform/GlContext.hpp>
#include <g7/platform/Window.hpp>
#include <g7/render/Device.hpp>

#include <doctest/doctest.h>

#include <memory>

namespace g7::test
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

template <typename T>
T require(Result<T> result)
{
    REQUIRE_MESSAGE(result.ok(), (result.ok() ? "" : result.error().message));
    return std::move(result).value();
}
} // namespace g7::test
