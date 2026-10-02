#include <g7/core/Log.hpp>
#include <g7/platform/Window.hpp>

#include <SDL3/SDL.h>

#include <limits>

namespace g7::platform
{
namespace
{
constexpr u32 kMaxDimension = static_cast<u32>(std::numeric_limits<int>::max());

Extent toExtent(int width, int height) noexcept
{
    return Extent{static_cast<u32>(width > 0 ? width : 0), static_cast<u32>(height > 0 ? height : 0)};
}
} // namespace

struct Window::Impl
{
    SDL_Window* window = nullptr;
    SDL_WindowID id = 0;
    WindowMode mode = WindowMode::Windowed;
    Extent lastPolledSize;
    Extent lastPolledPixelSize;
    bool resized = false;
    bool quitRequested = false;

    ~Impl()
    {
        if (window)
        {
            SDL_DestroyWindow(window);
        }
        // Balances SDL_InitSubSystem in create(); SDL ref-counts subsystems.
        SDL_QuitSubSystem(SDL_INIT_VIDEO);
    }
};

Window::Window(std::unique_ptr<Impl> impl) : m_impl(std::move(impl))
{
}
Window::~Window() = default;

Result<std::unique_ptr<Window>> Window::create(const WindowDesc& desc)
{
    if (desc.size.width == 0 || desc.size.height == 0 || desc.size.width > kMaxDimension ||
        desc.size.height > kMaxDimension)
    {
        return Error{"invalid window size"};
    }
    if (!SDL_InitSubSystem(SDL_INIT_VIDEO))
    {
        return Error{std::string("SDL video init failed: ") + SDL_GetError()};
    }

    auto impl = std::make_unique<Impl>();
    SDL_WindowFlags flags = SDL_WINDOW_HIGH_PIXEL_DENSITY;
    if (desc.resizable)
    {
        flags |= SDL_WINDOW_RESIZABLE;
    }
    if (desc.mode == WindowMode::Fullscreen)
    {
        flags |= SDL_WINDOW_FULLSCREEN; // no explicit display mode -> borderless desktop
    }
    impl->window = SDL_CreateWindow(desc.title.c_str(), static_cast<int>(desc.size.width),
                                    static_cast<int>(desc.size.height), flags);
    if (!impl->window)
    {
        return Error{std::string("SDL_CreateWindow failed: ") + SDL_GetError()};
    }
    impl->id = SDL_GetWindowID(impl->window);
    impl->mode = desc.mode;
    SDL_SyncWindow(impl->window);

    std::unique_ptr<Window> window(new Window(std::move(impl)));
    window->m_impl->lastPolledSize = window->size();
    window->m_impl->lastPolledPixelSize = window->pixelSize();
    G7_LOG_INFO("platform", "window {}x{} created (video driver '{}')", window->size().width,
                window->size().height, SDL_GetCurrentVideoDriver());
    return window;
}

bool Window::pollEvents()
{
    SDL_Event event;
    while (SDL_PollEvent(&event))
    {
        switch (event.type)
        {
        case SDL_EVENT_QUIT:
            m_impl->quitRequested = true;
            break;
        case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
            if (event.window.windowID == m_impl->id)
            {
                m_impl->quitRequested = true;
            }
            break;
        default:
            break;
        }
    }

    // Compare against the last poll instead of relying on resize events: setSize() and
    // fullscreen toggles are then reported the same way as user drags.
    const Extent current = size();
    const Extent currentPixels = pixelSize();
    m_impl->resized = current != m_impl->lastPolledSize || currentPixels != m_impl->lastPolledPixelSize;
    m_impl->lastPolledSize = current;
    m_impl->lastPolledPixelSize = currentPixels;

    return !m_impl->quitRequested;
}

Extent Window::size() const noexcept
{
    int width = 0;
    int height = 0;
    SDL_GetWindowSize(m_impl->window, &width, &height);
    return toExtent(width, height);
}

Extent Window::pixelSize() const noexcept
{
    int width = 0;
    int height = 0;
    SDL_GetWindowSizeInPixels(m_impl->window, &width, &height);
    return toExtent(width, height);
}

bool Window::resizedSinceLastPoll() const noexcept
{
    return m_impl->resized;
}

void Window::setSize(Extent size)
{
    if (size.width == 0 || size.height == 0 || size.width > kMaxDimension || size.height > kMaxDimension)
    {
        G7_LOG_WARN("platform", "ignoring invalid window size {}x{}", size.width, size.height);
        return;
    }
    SDL_SetWindowSize(m_impl->window, static_cast<int>(size.width), static_cast<int>(size.height));
    SDL_SyncWindow(m_impl->window);
}

void Window::setMode(WindowMode mode)
{
    if (mode == m_impl->mode)
    {
        return;
    }
    const bool fullscreen = mode == WindowMode::Fullscreen;
    if (fullscreen)
    {
        SDL_SetWindowFullscreenMode(m_impl->window, nullptr); // borderless desktop
    }
    if (!SDL_SetWindowFullscreen(m_impl->window, fullscreen))
    {
        G7_LOG_WARN("platform", "switching window mode failed: {}", SDL_GetError());
        return;
    }
    // Window state changes are asynchronous in SDL3; wait so size() reflects the new mode.
    SDL_SyncWindow(m_impl->window);
    m_impl->mode = mode;
    G7_LOG_DEBUG("platform", "window mode: {}", fullscreen ? "fullscreen" : "windowed");
}

WindowMode Window::mode() const noexcept
{
    return m_impl->mode;
}

void Window::setTitle(std::string_view title)
{
    SDL_SetWindowTitle(m_impl->window, std::string(title).c_str());
}

std::string Window::title() const
{
    return SDL_GetWindowTitle(m_impl->window);
}

void Window::requestClose()
{
    m_impl->quitRequested = true;
}
} // namespace g7::platform
