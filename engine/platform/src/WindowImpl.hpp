#pragma once

// Internal: Window::Impl, shared with GlContext (needs the SDL_Window).

#include <g7/platform/Window.hpp>

#include <SDL3/SDL.h>

namespace g7::platform
{
inline constexpr SDL_InitFlags kWindowSubsystems = SDL_INIT_VIDEO | SDL_INIT_GAMEPAD;

struct Window::Impl
{
    SDL_Window* window = nullptr;
    SDL_WindowID id = 0;
    WindowMode mode = WindowMode::Windowed;
    GraphicsApi graphics = GraphicsApi::None;
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
        // Balances SDL_InitSubSystem in Window::create(); SDL ref-counts subsystems.
        SDL_QuitSubSystem(kWindowSubsystems);
    }
};
} // namespace g7::platform
