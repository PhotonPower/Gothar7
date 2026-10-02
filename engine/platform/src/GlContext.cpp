#include "WindowImpl.hpp"

#include <g7/core/Log.hpp>
#include <g7/platform/GlContext.hpp>

#include <SDL3/SDL.h>

#include <string>

namespace g7::platform
{
struct GlContext::Impl
{
    SDL_Window* window = nullptr;
    SDL_GLContext context = nullptr;

    ~Impl()
    {
        if (context)
        {
            SDL_GL_DestroyContext(context);
        }
    }
};

GlContext::GlContext(std::unique_ptr<Impl> impl, i32 major, i32 minor)
    : m_impl(std::move(impl)), m_major(major), m_minor(minor)
{
}

GlContext::~GlContext() = default;

Result<std::unique_ptr<GlContext>> GlContext::create(Window& window, const GlContextDesc& desc)
{
    if (window.m_impl->graphics != GraphicsApi::OpenGL)
    {
        return Error{"window was not created with GraphicsApi::OpenGL"};
    }

    auto impl = std::make_unique<Impl>();
    impl->window = window.m_impl->window;
    std::string lastError;
    for (i32 minor = desc.minor; minor >= desc.minMinor; --minor)
    {
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, desc.major);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, minor);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG |
                                                      (desc.debug ? SDL_GL_CONTEXT_DEBUG_FLAG : 0));
        impl->context = SDL_GL_CreateContext(impl->window);
        if (impl->context)
        {
            if (!SDL_GL_MakeCurrent(impl->window, impl->context))
            {
                return Error{std::string("SDL_GL_MakeCurrent failed: ") + SDL_GetError()};
            }
            G7_LOG_INFO("platform", "OpenGL {}.{} core context created{}", desc.major, minor,
                        desc.debug ? " (debug)" : "");
            return std::unique_ptr<GlContext>(new GlContext(std::move(impl), desc.major, minor));
        }
        lastError = SDL_GetError();
        G7_LOG_DEBUG("platform", "OpenGL {}.{} core context not available: {}", desc.major, minor, lastError);
    }
    return Error{"no OpenGL " + std::to_string(desc.major) + "." + std::to_string(desc.minMinor) +
                 "+ core context available: " + lastError};
}

void GlContext::swapBuffers()
{
    SDL_GL_SwapWindow(m_impl->window);
}

namespace
{
f32 displayRefreshRate(SDL_Window* window) noexcept
{
    const SDL_DisplayMode* mode = SDL_GetCurrentDisplayMode(SDL_GetDisplayForWindow(window));
    return mode ? mode->refresh_rate : 0.0f;
}
} // namespace

bool GlContext::setVSync(bool enabled)
{
    if (!enabled)
    {
        return SDL_GL_SetSwapInterval(0);
    }
    if (SDL_GL_SetSwapInterval(-1))
    {
        G7_LOG_DEBUG("platform", "adaptive VSync enabled (display {:.0f} Hz)",
                     displayRefreshRate(m_impl->window));
        return true;
    }
    if (SDL_GL_SetSwapInterval(1))
    {
        G7_LOG_DEBUG("platform", "VSync enabled (display {:.0f} Hz)", displayRefreshRate(m_impl->window));
        return true;
    }
    G7_LOG_WARN("platform", "VSync not available: {}", SDL_GetError());
    return false;
}

GlContext::ProcAddress GlContext::procAddress(const char* name) noexcept
{
    return SDL_GL_GetProcAddress(name);
}
} // namespace g7::platform
