#include <g7/core/Log.hpp>
#include <g7/runtime/Engine.hpp>

#include <cstdlib>
#include <string_view>

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

    g7::Engine engine(config);
    if (auto result = engine.init(); !result)
    {
        G7_LOG_FATAL("game", "engine init failed: {}", result.error().message);
        return EXIT_FAILURE;
    }
    return engine.run();
}
