#include <g7/core/FileSystem.hpp>
#include <g7/core/Log.hpp>
#include <g7/runtime/Engine.hpp>

#include <cstdlib>
#include <filesystem>
#include <string_view>
#include <system_error>

int main(int argc, char** argv)
{
    g7::EngineConfig config;
    config.appName = "Gothar";

    for (int i = 1; i < argc; ++i)
    {
        const std::string_view arg = argv[i];
        if (arg == "--verbose")
        {
            g7::log::setMinLevel(g7::log::Level::Debug);
        }
        else if (arg == "--smoke-test")
        {
            // Headless run used by CI: a few frames, then exit.
            config.maxFrames = 10;
        }
    }

    // Interim until M1: the platform module will set the user directory via SDL_GetPrefPath.
    std::error_code ec;
    g7::fs::Path gameDir = std::filesystem::weakly_canonical(g7::fs::Path(argv[0]), ec).parent_path();
    if (ec || gameDir.empty())
    {
        gameDir = std::filesystem::current_path(ec);
    }
    g7::fs::setBaseDirectories({.gameDir = gameDir, .userDir = gameDir / "userdata"});
    G7_LOG_DEBUG("game", "game directory: {}", g7::fs::toUtf8(gameDir));

    g7::Engine engine(config);
    if (auto result = engine.init(); !result)
    {
        G7_LOG_FATAL("game", "engine init failed: {}", result.error().message);
        return EXIT_FAILURE;
    }
    return engine.run();
}
