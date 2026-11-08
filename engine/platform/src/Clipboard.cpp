#include <g7/platform/Clipboard.hpp>

#include <SDL3/SDL.h>

#include <filesystem>
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

Result<void> openFolder(const fs::Path& folder)
{
    std::string url = "file:///" + fs::toUtf8(std::filesystem::absolute(folder));
    for (char& c : url)
    {
        c = c == '\\' ? '/' : c;
    }
    if (!SDL_OpenURL(url.c_str()))
    {
        return Error{std::string("open folder: ") + SDL_GetError()};
    }
    return {};
}
} // namespace g7::platform
