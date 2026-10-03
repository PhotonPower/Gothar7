#include <g7/platform/Clipboard.hpp>

#include <SDL3/SDL.h>

#include <string>

namespace g7::platform
{
Result<void> setClipboardText(std::string_view text)
{
    const std::string copy(text); // SDL wants a terminated string
    if (!SDL_SetClipboardText(copy.c_str()))
    {
        return Error{std::string("clipboard: ") + SDL_GetError()};
    }
    return {};
}
} // namespace g7::platform
