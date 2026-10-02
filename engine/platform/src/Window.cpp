#include "SdlInput.hpp"

#include <g7/core/Log.hpp>
#include <g7/platform/Window.hpp>

#include <SDL3/SDL.h>

#include <limits>

namespace g7::platform
{
namespace
{
constexpr SDL_InitFlags kSubsystems = SDL_INIT_VIDEO | SDL_INIT_GAMEPAD;
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
    SDL_Gamepad* gamepad = nullptr; // single player: the first connected pad is used
    SDL_JoystickID gamepadId = 0;

    ~Impl()
    {
        if (gamepad)
        {
            SDL_CloseGamepad(gamepad);
        }
        if (window)
        {
            SDL_DestroyWindow(window);
        }
        // Balances SDL_InitSubSystem in create(); SDL ref-counts subsystems.
        SDL_QuitSubSystem(kSubsystems);
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
    if (!SDL_InitSubSystem(kSubsystems))
    {
        return Error{std::string("SDL video/gamepad init failed: ") + SDL_GetError()};
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

namespace
{
void openFirstGamepad(SDL_Gamepad*& gamepad, SDL_JoystickID& gamepadId, Input& input)
{
    int count = 0;
    SDL_JoystickID* ids = SDL_GetGamepads(&count);
    for (int i = 0; i < count && !gamepad; ++i)
    {
        gamepad = SDL_OpenGamepad(ids[i]);
        if (gamepad)
        {
            gamepadId = ids[i];
            G7_LOG_INFO("platform", "gamepad connected: {}", SDL_GetGamepadName(gamepad));
        }
    }
    SDL_free(ids);
    input.onGamepadConnected(gamepad != nullptr);
}
} // namespace

bool Window::pollEvents()
{
    Input ignored;
    return pollEvents(ignored);
}

bool Window::pollEvents(Input& input)
{
    Impl& impl = *m_impl;
    SDL_Event event;
    while (SDL_PollEvent(&event))
    {
        switch (event.type)
        {
        case SDL_EVENT_QUIT:
            impl.quitRequested = true;
            break;
        case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
            if (event.window.windowID == impl.id)
            {
                impl.quitRequested = true;
            }
            break;
        case SDL_EVENT_WINDOW_FOCUS_LOST:
            // Key-up events for keys held during Alt+Tab never arrive.
            input.releaseAll();
            break;
        case SDL_EVENT_KEY_DOWN:
        case SDL_EVENT_KEY_UP:
            if (!event.key.repeat)
            {
                input.onKey(sdl::toKey(event.key.scancode), event.key.down);
            }
            break;
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
        case SDL_EVENT_MOUSE_BUTTON_UP:
            if (const auto button = sdl::toMouseButton(event.button.button))
            {
                input.onMouseButton(*button, event.button.down);
            }
            break;
        case SDL_EVENT_MOUSE_MOTION:
            input.onMouseMotion(Vec2(event.motion.x, event.motion.y),
                                Vec2(event.motion.xrel, event.motion.yrel));
            break;
        case SDL_EVENT_MOUSE_WHEEL:
            input.onWheel(event.wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -event.wheel.y : event.wheel.y);
            break;
        case SDL_EVENT_GAMEPAD_ADDED:
            if (!impl.gamepad)
            {
                openFirstGamepad(impl.gamepad, impl.gamepadId, input);
            }
            break;
        case SDL_EVENT_GAMEPAD_REMOVED:
            if (impl.gamepad && event.gdevice.which == impl.gamepadId)
            {
                G7_LOG_INFO("platform", "gamepad disconnected");
                SDL_CloseGamepad(impl.gamepad);
                impl.gamepad = nullptr;
                impl.gamepadId = 0;
                input.onGamepadConnected(false);
                openFirstGamepad(impl.gamepad, impl.gamepadId, input); // fall back to another pad
            }
            break;
        case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
        case SDL_EVENT_GAMEPAD_BUTTON_UP:
            if (event.gbutton.which == impl.gamepadId)
            {
                if (const auto button = sdl::toGamepadButton(event.gbutton.button))
                {
                    input.onGamepadButton(*button, event.gbutton.down);
                }
            }
            break;
        case SDL_EVENT_GAMEPAD_AXIS_MOTION:
            if (event.gaxis.which == impl.gamepadId)
            {
                if (const auto axis = sdl::toGamepadAxis(event.gaxis.axis))
                {
                    input.onGamepadAxis(*axis, sdl::normalizeAxis(*axis, event.gaxis.value));
                }
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

bool Window::setRelativeMouse(bool enabled)
{
    if (!SDL_SetWindowRelativeMouseMode(m_impl->window, enabled))
    {
        G7_LOG_WARN("platform", "relative mouse mode not available: {}", SDL_GetError());
        return false;
    }
    return true;
}

bool Window::relativeMouse() const noexcept
{
    return SDL_GetWindowRelativeMouseMode(m_impl->window);
}

void Window::requestClose()
{
    m_impl->quitRequested = true;
}
} // namespace g7::platform
