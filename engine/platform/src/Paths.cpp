#include <g7/platform/Paths.hpp>

#include <SDL3/SDL.h>

#include <string>

namespace g7::platform
{
Result<fs::Path> userDataDirectory(std::string_view org, std::string_view app)
{
    char* path = SDL_GetPrefPath(std::string(org).c_str(), std::string(app).c_str());
    if (!path)
    {
        return Error{std::string("SDL_GetPrefPath failed: ") + SDL_GetError()};
    }
    // SDL returns UTF-8 with a trailing separator; drop it so the path names the directory.
    std::string utf8(path);
    SDL_free(path);
    while (!utf8.empty() && (utf8.back() == '/' || utf8.back() == '\\'))
    {
        utf8.pop_back();
    }
    return fs::fromUtf8(utf8);
}
} // namespace g7::platform
