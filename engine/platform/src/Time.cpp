#include <g7/platform/Time.hpp>

#include <SDL3/SDL.h>

namespace g7::platform
{
f64 nowSeconds() noexcept
{
    return static_cast<f64>(SDL_GetTicksNS()) / 1e9;
}

void sleepPrecise(f64 seconds) noexcept
{
    if (seconds > 0.0)
    {
        SDL_DelayPrecise(static_cast<Uint64>(seconds * 1e9));
    }
}
} // namespace g7::platform
