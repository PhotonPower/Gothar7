#include <g7/core/FileSystem.hpp>
#include <g7/core/Log.hpp>
#include <g7/platform/Paths.hpp>
#include <g7/runtime/Engine.hpp>

#include <charconv>
#include <cstdlib>
#include <filesystem>
#include <string_view>
#include <system_error>

int main(int argc, char** argv)
{
    g7::EngineConfig config;
    config.appName = "Gothar";
    config.window.title = "Gothar";

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
            config.headless = true;
            config.maxFrames = 10;
        }
        else if (arg == "--fullscreen")
        {
            config.window.mode = g7::platform::WindowMode::Fullscreen;
        }
        else if (arg.starts_with("--frames="))
        {
            const std::string_view value = arg.substr(9);
            g7::u64 frames = 0;
            const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), frames);
            if (error != std::errc{} || end != value.data() + value.size())
            {
                G7_LOG_FATAL("game", "invalid value for --frames: '{}'", value);
                return EXIT_FAILURE;
            }
            config.maxFrames = frames;
        }
        else
        {
            G7_LOG_WARN("game", "unknown argument '{}'", arg);
        }
    }

    std::error_code ec;
    g7::fs::Path gameDir = std::filesystem::weakly_canonical(g7::fs::Path(argv[0]), ec).parent_path();
    if (ec || gameDir.empty())
    {
        gameDir = std::filesystem::current_path(ec);
    }
    g7::fs::Path userDir = gameDir / "userdata";
    if (auto prefPath = g7::platform::userDataDirectory("Gothar", "Gothar"))
    {
        userDir = std::move(prefPath).value();
    }
    else
    {
        G7_LOG_WARN("game", "{} - falling back to {}", prefPath.error().message, g7::fs::toUtf8(userDir));
    }
    g7::fs::setBaseDirectories({.gameDir = gameDir, .userDir = userDir});
    G7_LOG_DEBUG("game", "game directory: {}", g7::fs::toUtf8(gameDir));
    G7_LOG_DEBUG("game", "user directory: {}", g7::fs::toUtf8(userDir));

    g7::Engine engine(config);
    if (auto result = engine.init(); !result)
    {
        G7_LOG_FATAL("game", "engine init failed: {}", result.error().message);
        return EXIT_FAILURE;
    }
    return engine.run();
}
