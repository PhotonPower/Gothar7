#include <g7/core/Config.hpp>
#include <g7/core/FileSystem.hpp>
#include <g7/core/Log.hpp>
#include <g7/editor/Editor.hpp>
#include <g7/platform/GpuPreference.hpp>
#include <g7/platform/Paths.hpp>
#include <g7/runtime/Engine.hpp>

#include <algorithm>
#include <charconv>
#include <cstdlib>
#include <filesystem>
#include <limits>
#include <optional>
#include <string_view>
#include <system_error>

// Run on the dedicated GPU of dual-GPU laptops (see GpuPreference.hpp).
G7_REQUEST_HIGH_PERFORMANCE_GPU();

namespace
{
struct CommandLine
{
    bool smokeTest = false;
    bool fullscreen = false;
    bool noRender = false;
    bool noSun = false;
    bool noGround = false;
    std::string viewMesh;
    std::string scene;
    std::string world;
    std::string saveWorld;
    std::string start;
    std::string time;
    std::string screenshot;
    bool benchmark = false;
    bool editor = false;
    std::optional<g7::u32> viewpoint;
    std::optional<g7::u64> frames;
    std::optional<g7::u64> maxFps;
};

std::optional<CommandLine> parseCommandLine(int argc, char** argv)
{
    CommandLine cli;
    for (int i = 1; i < argc; ++i)
    {
        const std::string_view arg = argv[i];
        if (arg == "--verbose")
        {
            g7::log::setMinLevel(g7::log::Level::Debug);
        }
        else if (arg == "--smoke-test")
        {
            cli.smokeTest = true;
        }
        else if (arg == "--no-ground")
        {
            cli.noGround = true;
        }
        else if (arg == "--no-sun")
        {
            cli.noSun = true;
        }
        else if (arg == "--no-render")
        {
            cli.noRender = true;
        }
        else if (arg.starts_with("--world="))
        {
            cli.world = std::string(arg.substr(8));
        }
        else if (arg.starts_with("--save-world="))
        {
            cli.saveWorld = std::string(arg.substr(13));
        }
        else if (arg.starts_with("--start="))
        {
            cli.start = std::string(arg.substr(8));
        }
        else if (arg.starts_with("--time="))
        {
            cli.time = std::string(arg.substr(7));
        }
        else if (arg.starts_with("--scene="))
        {
            cli.scene = std::string(arg.substr(8));
        }
        else if (arg.starts_with("--screenshot="))
        {
            cli.screenshot = std::string(arg.substr(13));
        }
        else if (arg.starts_with("--viewpoint="))
        {
            const std::string_view value = arg.substr(12);
            g7::u32 index = 0;
            const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), index);
            if (error != std::errc{} || end != value.data() + value.size())
            {
                G7_LOG_FATAL("game", "invalid value for --viewpoint: '{}'", value);
                return std::nullopt;
            }
            cli.viewpoint = index;
        }
        else if (arg == "--benchmark")
        {
            cli.benchmark = true;
        }
        else if (arg == "--editor")
        {
            cli.editor = true;
        }
        else if (arg.starts_with("--view-mesh="))
        {
            cli.viewMesh = std::string(arg.substr(12));
        }
        else if (arg == "--fullscreen")
        {
            cli.fullscreen = true;
        }
        else if (arg.starts_with("--max-fps="))
        {
            const std::string_view value = arg.substr(10);
            g7::u64 fps = 0;
            const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), fps);
            if (error != std::errc{} || end != value.data() + value.size())
            {
                G7_LOG_FATAL("game", "invalid value for --max-fps: '{}'", value);
                return std::nullopt;
            }
            cli.maxFps = fps;
        }
        else if (arg.starts_with("--frames="))
        {
            const std::string_view value = arg.substr(9);
            g7::u64 frames = 0;
            const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), frames);
            if (error != std::errc{} || end != value.data() + value.size())
            {
                G7_LOG_FATAL("game", "invalid value for --frames: '{}'", value);
                return std::nullopt;
            }
            cli.frames = frames;
        }
        else
        {
            G7_LOG_WARN("game", "unknown argument '{}'", arg);
        }
    }
    return cli;
}

void setupDirectories(const char* argv0)
{
    std::error_code ec;
    g7::fs::Path gameDir = std::filesystem::weakly_canonical(g7::fs::Path(argv0), ec).parent_path();
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
}

/// Default config shipped with the game, overridden key by key by the user config.
g7::Config loadSettings()
{
    g7::Config settings;
    if (auto defaults = g7::Config::load(g7::fs::gamePath("config/engine.toml")))
    {
        settings = std::move(defaults).value();
    }
    else
    {
        G7_LOG_WARN("game", "default config not loaded ({}), using built-in defaults",
                    defaults.error().message);
    }

    const g7::fs::Path userConfig = g7::fs::userPath("config.toml");
    if (g7::fs::exists(userConfig))
    {
        if (auto user = g7::Config::load(userConfig))
        {
            settings.merge(user.value());
            G7_LOG_INFO("game", "user config applied: {}", g7::fs::toUtf8(userConfig));
        }
        else
        {
            G7_LOG_WARN("game", "user config ignored: {}", user.error().message);
        }
    }
    return settings;
}

g7::u32 toDimension(g7::i64 value, g7::u32 fallback)
{
    return value > 0 && value <= std::numeric_limits<int>::max() ? static_cast<g7::u32>(value) : fallback;
}
} // namespace

int main(int argc, char** argv)
{
    const auto cli = parseCommandLine(argc, argv);
    if (!cli)
    {
        return EXIT_FAILURE;
    }
    setupDirectories(argv[0]);

    g7::EngineConfig config;
    config.appName = "Gothar";
    config.settings = loadSettings();

    config.window.title = "Gothar";
    config.window.size.width = toDimension(config.settings.get<g7::i64>("window.width", 1600), 1600);
    config.window.size.height = toDimension(config.settings.get<g7::i64>("window.height", 900), 900);
    if (config.settings.get<bool>("window.fullscreen", false))
    {
        config.window.mode = g7::platform::WindowMode::Fullscreen;
    }
    config.window.vsync = config.settings.get<bool>("window.vsync", true);
    if (const auto shaderDir = config.settings.find<std::string>("render.shader_dir"))
    {
        config.shaderDirectory = g7::fs::fromUtf8(*shaderDir);
    }
    config.maxFps =
        static_cast<g7::f64>(std::max<g7::i64>(0, config.settings.get<g7::i64>("window.max_fps", 240)));

    // Command line wins over config files.
    if (cli->fullscreen)
    {
        config.window.mode = g7::platform::WindowMode::Fullscreen;
    }
    if (!cli->viewMesh.empty())
    {
        config.viewMesh = g7::fs::fromUtf8(cli->viewMesh);
    }
    if (!cli->world.empty())
    {
        config.world = g7::fs::fromUtf8(cli->world);
    }
    config.start = cli->start;
    config.startTime = cli->time;
    if (!cli->saveWorld.empty())
    {
        config.saveWorld = g7::fs::fromUtf8(cli->saveWorld);
    }
    if (!cli->scene.empty())
    {
        config.scene = g7::fs::fromUtf8(cli->scene);
    }
    if (!cli->screenshot.empty())
    {
        config.screenshot = g7::fs::fromUtf8(cli->screenshot);
    }
    if (cli->viewpoint)
    {
        config.viewpoint = *cli->viewpoint;
    }
    if (cli->benchmark)
    {
        // Measure what the GPU can do: no VSync, no frame cap.
        config.benchmark = true;
        config.window.vsync = false;
        config.maxFps = 0.0;
    }
    if (cli->noGround)
    {
        config.ground = false;
    }
    if (cli->noSun)
    {
        config.sun = false;
    }
    if (cli->noRender)
    {
        config.render = false;
    }
    if (cli->maxFps)
    {
        config.maxFps = static_cast<g7::f64>(*cli->maxFps);
    }
    if (cli->frames)
    {
        config.maxFrames = *cli->frames;
    }
    if (cli->smokeTest)
    {
        // Headless run used by CI: a few frames, then exit.
        config.headless = true;
        config.maxFrames = 10;
        config.fixedFrameSeconds = 1.0 / 60.0; // deterministic: 10 frames = 10 ticks
    }

    g7::Engine engine(std::move(config));
    if (auto result = engine.init(); !result)
    {
        G7_LOG_FATAL("game", "engine init failed: {}", result.error().message);
        return EXIT_FAILURE;
    }
    // Editor mode (docs/modules/tools.md): the same engine with the editor as a tool.
    std::optional<g7::editor::Editor> editor;
    if (cli->editor)
    {
        editor.emplace(engine);
        engine.addTool(*editor);
    }
    return engine.run();
}
