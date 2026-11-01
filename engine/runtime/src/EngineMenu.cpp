// Main menu with the camera flight, pause menu, options and loading screen (M14 part F, docs/modules/ui.md).
#include <g7/audio/Audio.hpp>
#include <g7/core/Log.hpp>
#include <g7/core/StringUtil.hpp>
#include <g7/platform/GlContext.hpp>
#include <g7/platform/Window.hpp>
#include <g7/runtime/Engine.hpp>
#include <g7/ui/MenuScreens.hpp>

#include <algorithm>
#include <charconv>
#include <format>
#include <utility>

namespace g7
{
namespace
{
/// Actions the player may assign keys to (F11: the gamepad stays as it is for now).
constexpr platform::Action kAssignable[] = {
    platform::Action::MoveForward, platform::Action::MoveBack,   platform::Action::TurnLeft,
    platform::Action::TurnRight,   platform::Action::StrafeLeft, platform::Action::StrafeRight,
    platform::Action::Action,      platform::Action::Jump,       platform::Action::Walk,
    platform::Action::Sneak,       platform::Action::DrawWeapon, platform::Action::DrawRanged,
    platform::Action::DrawMagic,   platform::Action::Inventory,  platform::Action::Log,
    platform::Action::Status,      platform::Action::Map,
};

/// Seconds the loading screen waits at most for textures still loading in the background.
constexpr f64 kLoadingPatience = 8.0;

std::string actionLabelKey(platform::Action action)
{
    return "UI_ACTION_" + toUpper(platform::name(action));
}

ui::OptionDef slider(std::string id, std::string label, f64 min = 0.0, f64 max = 1.0, f64 step = 0.05)
{
    return {std::move(id), std::move(label), ui::OptionKind::Slider, min, max, step, {}};
}

ui::OptionDef toggle(std::string id, std::string label)
{
    return {std::move(id), std::move(label), ui::OptionKind::Toggle, 0.0, 1.0, 0.0, {}};
}

ui::OptionDef choice(std::string id, std::string label, std::vector<ui::OptionChoice> choices)
{
    return {std::move(id), std::move(label), ui::OptionKind::Choice, 0.0, 1.0, 0.0, std::move(choices)};
}

/// Options whose value is not one config key.
constexpr std::string_view kResolution = "window.resolution";
} // namespace

// --- Start, new game, loading ---

void Engine::startMainMenu()
{
    const std::string world = m_config.settings.get<std::string>("menu.world", "");
    if (!m_gameUi || world.empty())
    {
        // Without the game UI there is no menu to show: straight into the game.
        G7_LOG_WARN("ui", "main menu not available ({}), starting a new game",
                    m_gameUi ? "no [menu] world" : "no game UI");
        startNewGame();
        return;
    }
    m_menuMode = true;
    m_config.player = false; // the menu world has nobody in it
    if (auto text = m_vfs.read(m_config.settings.get<std::string>("menu.flight", "ui/menu_flight.toml")))
    {
        const std::string_view toml(reinterpret_cast<const char*>(text.value().data()), text.value().size());
        auto config = Config::parse(toml, "menu_flight.toml");
        auto flight = config ? MenuFlight::fromConfig(config.value()) : Result<MenuFlight>(config.error());
        if (flight)
        {
            m_menuFlight = std::move(flight).value();
        }
        else
        {
            G7_LOG_WARN("ui", "{}", flight.error().message);
        }
    }
    beginLoading(world, m_config.settings.get<std::string>("menu.start", ""),
                 m_config.settings.get<std::string>("menu.title", "Leonberg"), true);
}

void Engine::startNewGame()
{
    const std::string world = m_config.settings.get<std::string>("game.world", "testworld/camp.g7world");
    m_config.player = true;
    beginLoading(world, m_config.settings.get<std::string>("game.start", ""),
                 m_config.settings.get<std::string>("game.title", ""), false);
}

void Engine::beginLoading(std::string world, std::string start, std::string title, bool menu)
{
    m_screens.clear();
    m_loadJob = LoadJob{std::move(world), std::move(start), std::move(title), menu};
    if (m_gameUi)
    {
        m_screens.push(std::make_unique<ui::LoadingScreen>(
            [this]
            {
                ui::LoadingView view;
                if (m_loadJob)
                {
                    view.world = m_loadJob->title;
                    view.progress = m_loadJob->progress;
                }
                return view;
            }));
    }
}

void Engine::updateLoading(f64 realSeconds)
{
    if (!m_loadJob)
    {
        return;
    }
    LoadJob& job = *m_loadJob;
    ++job.frames;
    if (!job.requested)
    {
        // The loading screen is drawn at least once before the world loads synchronously.
        if (job.frames >= 2 || !m_gameUi)
        {
            G7_LOG_INFO("engine", "loading {}{}", job.world,
                        job.menu   ? " (main menu)"
                        : job.save ? " (save)"
                                   : "");
            if (job.save)
            {
                if (auto applied = applyLoadedSave(*job.save); !applied)
                {
                    G7_LOG_ERROR("save", "{}", applied.error().message);
                    notice(uiText("UI_LOAD_FAILED"));
                }
            }
            else
            {
                requestWorldChange(job.world, job.start); // happens at the end of this frame
            }
            job.requested = true;
            job.progress = 0.1f;
        }
        return;
    }
    if (!job.loaded)
    {
        job.loaded = true;
        job.pending = m_assets ? m_assets->pendingCount() : 0;
        job.seconds = 0.0;
        if (!equalsIgnoreCase(m_worldPath, job.world))
        {
            G7_LOG_ERROR("engine", "loading {} failed", job.world);
        }
    }
    job.seconds += realSeconds;
    const usize pending = m_assets ? m_assets->pendingCount() : 0;
    job.progress = job.pending == 0 ? 1.0f
                                    : 0.5f + 0.5f * (1.0f - static_cast<f32>(std::min(pending, job.pending)) /
                                                                static_cast<f32>(job.pending));
    if (pending > 0 && job.seconds < kLoadingPatience)
    {
        return;
    }
    const bool menu = job.menu;
    m_loadJob.reset();
    if (ui::Screen* loading = m_screens.find("loading"))
    {
        loading->close();
    }
    if (menu)
    {
        m_menuMode = true;
        m_menuFlightTime = 0.0f;
        m_musicForcedTheme = m_config.settings.get<std::string>("menu.music", "MENU");
        m_musicForcedState = audio::MusicState::Std;
        openMenu(false);
    }
    else
    {
        m_menuMode = false; // the menu world was not kept (performWorldChange skips it)
        m_musicForcedTheme.reset();
        m_musicForcedState.reset();
    }
}

// --- Menus ---

void Engine::openMenu(bool inGame)
{
    std::vector<ui::MenuEntry> entries;
    if (inGame)
    {
        entries = {{"resume", "UI_MENU_RESUME", true},
                   {"new_game", "UI_MENU_NEW_GAME", false}, // a second game in one run: not yet
                   {"load", "UI_MENU_LOAD", anySave()},
                   {"save", "UI_MENU_SAVE", saveBlocker().empty()},
                   {"options", "UI_MENU_OPTIONS", true},
                   {"quit", "UI_MENU_QUIT", true}};
    }
    else
    {
        entries = {{"new_game", "UI_MENU_NEW_GAME", true},
                   {"load", "UI_MENU_LOAD", anySave()},
                   {"options", "UI_MENU_OPTIONS", true},
                   {"quit", "UI_MENU_QUIT", true}};
    }
    ui::MenuScreen::Actions actions;
    // Screens are only changed after the stack's update (the callbacks run inside it).
    actions.choose = [this](std::string_view id) { m_menuCommand = std::string(id); };
    if (inGame)
    {
        actions.back = [this] { m_menuCommand = "resume"; };
    }
    m_screens.push(std::make_unique<ui::MenuScreen>(std::move(entries), std::move(actions), !inGame));
}

void Engine::openPauseMenu()
{
    if (m_gameUi && !m_menuMode && !m_loadJob && m_screens.find("menu") == nullptr)
    {
        m_thumbnailRequested = m_device != nullptr; // S7: the scene as it was before the menu opened
        openMenu(true);
    }
}

void Engine::runMenuCommand()
{
    if (m_menuCommand.empty())
    {
        return;
    }
    const std::string command = std::exchange(m_menuCommand, {});
    if (command == "resume")
    {
        if (ui::Screen* menu = m_screens.find("menu"))
        {
            menu->close();
        }
    }
    else if (command == "new_game")
    {
        startNewGame();
    }
    else if (command == "options")
    {
        openOptions();
    }
    else if (command == "save" || command == "load")
    {
        openSaveScreen(command == "save");
    }
    else if (command.starts_with("save_to:") || command.starts_with("load_from:"))
    {
        runSaveCommand(command);
    }
    else if (command == "quit")
    {
        requestQuit();
    }
}

void Engine::updateMenu(f64 realSeconds)
{
    runMenuCommand();
    updateLoading(realSeconds);
}

void Engine::updateMenuFlight(f64 realSeconds)
{
    if (m_menuFlight.empty())
    {
        return;
    }
    m_menuFlightTime += static_cast<f32>(realSeconds);
    const MenuFlightPose pose = m_menuFlight.at(m_menuFlightTime);
    const Vec3 direction = pose.look - pose.position;
    if (glm::length(direction) < 1e-3f)
    {
        return;
    }
    const Vec3 d = glm::normalize(direction);
    m_camera.transform.position = pose.position;
    m_flyCamera.attach(m_camera);
    m_flyCamera.setOrientation(m_camera, std::atan2(-d.x, -d.z), std::asin(std::clamp(d.y, -1.0f, 1.0f)));
}

// --- Options ---

std::vector<ui::OptionPage> Engine::optionPages() const
{
    using ui::OptionChoice;
    using ui::UiText;
    std::vector<OptionChoice> resolutions;
    for (const char* size : {"1280x720", "1600x900", "1920x1080", "2560x1440", "3840x2160"})
    {
        resolutions.push_back({std::string(size), UiText::literal(std::string(size))});
    }
    std::vector<OptionChoice> fps{{i64{0}, UiText::key("UI_UNLIMITED")}};
    for (const i64 value : {30, 60, 120, 144, 240})
    {
        fps.push_back({value, UiText::literal(std::to_string(value))});
    }
    std::vector<OptionChoice> distances;
    for (const f64 value : {200.0, 300.0, 400.0, 600.0, 800.0})
    {
        distances.push_back({value, UiText::literal(std::format("{} m", static_cast<int>(value)))});
    }
    std::vector<OptionChoice> scales;
    for (const f64 value : {0.75, 0.9, 1.0, 1.1, 1.25, 1.5})
    {
        scales.push_back(
            {value, UiText::literal(std::format("{} %", static_cast<int>(value * 100.0 + 0.5)))});
    }

    std::vector<ui::OptionPage> pages(4);
    pages[0] = {"UI_OPTIONS_GRAPHICS",
                {choice(std::string(kResolution), "UI_OPT_RESOLUTION", resolutions),
                 toggle("window.fullscreen", "UI_OPT_FULLSCREEN"), toggle("window.vsync", "UI_OPT_VSYNC"),
                 choice("window.max_fps", "UI_OPT_MAX_FPS", fps),
                 choice("render.view_distance", "UI_OPT_VIEW_DISTANCE", distances)}};
    pages[1] = {"UI_OPTIONS_AUDIO",
                {slider("audio.master", "UI_OPT_MASTER"), slider("audio.music", "UI_OPT_MUSIC"),
                 slider("audio.effects", "UI_OPT_EFFECTS"), slider("audio.voice", "UI_OPT_VOICE"),
                 slider("audio.ambient", "UI_OPT_AMBIENT"), slider("audio.ui", "UI_OPT_UI")}};
    pages[2].title = "UI_OPTIONS_CONTROLS";
    pages[2].options = {choice("input.scheme", "UI_OPT_SCHEME",
                               {{std::string("classic"), UiText::key("UI_OPT_SCHEME_CLASSIC")},
                                {std::string("modern"), UiText::key("UI_OPT_SCHEME_MODERN")}}),
                        slider("input.mouse_sensitivity", "UI_OPT_MOUSE", 0.25, 2.0, 0.05)};
    for (const platform::Action action : kAssignable)
    {
        pages[2].options.push_back({std::string(platform::name(action)),
                                    actionLabelKey(action),
                                    ui::OptionKind::Binding,
                                    0.0,
                                    1.0,
                                    0.0,
                                    {}});
    }
    pages[3] = {"UI_OPTIONS_GAME",
                {choice("ui.language", "UI_OPT_LANGUAGE",
                        {{std::string("de"), UiText::literal("Deutsch")},
                         {std::string("en"), UiText::literal("English")}}),
                 toggle("ui.subtitles", "UI_OPT_SUBTITLES"), toggle("game.autosave", "UI_OPT_AUTOSAVE"),
                 choice("ui.scale", "UI_OPT_UI_SCALE", scales)}};
    return pages;
}

ui::OptionValue Engine::optionValue(const ui::OptionDef& option) const
{
    const Config& s = m_config.settings;
    if (option.id == kResolution)
    {
        return std::format("{}x{}", s.get<i64>("window.width", 1600), s.get<i64>("window.height", 900));
    }
    switch (option.kind)
    {
    case ui::OptionKind::Slider:
        return s.get<f64>(option.id, 1.0);
    case ui::OptionKind::Toggle:
        return s.get<bool>(option.id, true);
    case ui::OptionKind::Choice:
        if (!option.choices.empty() && std::holds_alternative<i64>(option.choices.front().value))
        {
            return s.get<i64>(option.id, 0);
        }
        if (!option.choices.empty() && std::holds_alternative<f64>(option.choices.front().value))
        {
            return s.get<f64>(option.id, 1.0);
        }
        return s.get<std::string>(option.id, "");
    case ui::OptionKind::Binding:
        break;
    }
    return std::string();
}

void Engine::setOption(const ui::OptionDef& option, const ui::OptionValue& value)
{
    Config& s = m_config.settings;
    if (option.id == kResolution)
    {
        const std::string text =
            std::holds_alternative<std::string>(value) ? std::get<std::string>(value) : "";
        const usize x = text.find('x');
        int width = 0;
        int height = 0;
        if (x == std::string::npos ||
            std::from_chars(text.data(), text.data() + x, width).ec != std::errc{} ||
            std::from_chars(text.data() + x + 1, text.data() + text.size(), height).ec != std::errc{} ||
            width <= 0 || height <= 0)
        {
            return;
        }
        s.set<i64>("window.width", width);
        s.set<i64>("window.height", height);
        m_changedSettings.insert("window.width");
        m_changedSettings.insert("window.height");
        applySetting(kResolution);
        return;
    }
    std::visit(
        [&](const auto& v)
        {
            using T = std::decay_t<decltype(v)>;
            s.set<T>(option.id, v);
        },
        value);
    m_changedSettings.insert(option.id);
    applySetting(option.id);
}

void Engine::applySetting(std::string_view key)
{
    const Config& s = m_config.settings;
    if (key == kResolution || key == "window.fullscreen")
    {
        if (m_window)
        {
            const bool fullscreen = s.get<bool>("window.fullscreen", false);
            m_window->setMode(fullscreen ? platform::WindowMode::Fullscreen : platform::WindowMode::Windowed);
            if (!fullscreen)
            {
                m_window->setSize({static_cast<u32>(s.get<i64>("window.width", 1600)),
                                   static_cast<u32>(s.get<i64>("window.height", 900))});
            }
        }
    }
    else if (key == "window.vsync")
    {
        if (m_glContext)
        {
            (void)m_glContext->setVSync(s.get<bool>("window.vsync", true));
        }
    }
    else if (key == "window.max_fps")
    {
        m_config.maxFps = static_cast<f64>(std::max<i64>(0, s.get<i64>("window.max_fps", 240)));
        m_framePacer = FramePacer(m_config.maxFps);
    }
    else if (key == "render.view_distance")
    {
        m_cullSettings.viewDistance = static_cast<f32>(s.get<f64>("render.view_distance", 400.0));
    }
    else if (key.starts_with("audio.") && m_audio)
    {
        const std::string_view name = key.substr(6);
        const f32 volume = static_cast<f32>(s.get<f64>(key, 1.0));
        if (name == "master")
        {
            m_audio->setMasterVolume(volume);
        }
        else if (const auto bus = audio::busFromName(name))
        {
            m_audio->setBusVolume(*bus, volume); // the music bus also follows the ducking each frame
        }
    }
    else if (key == "input.scheme")
    {
        const std::string scheme = s.get<std::string>("input.scheme", "classic");
        m_actions = platform::ActionMap::fromConfig(s, scheme);
        G7_LOG_INFO("engine", "control scheme '{}'", scheme);
    }
    else if (key == "ui.language")
    {
        loadTexts();
        reloadGameUi();
    }
}

std::string Engine::bindingText(const ui::OptionDef& option) const
{
    const auto action = platform::actionFromName(option.id);
    if (!action)
    {
        return {};
    }
    std::string text;
    for (const platform::InputBinding& binding : m_actions.bindings(*action))
    {
        text += (text.empty() ? "" : ", ") + std::string(platform::name(binding));
    }
    return text;
}

std::string Engine::assignBinding(const ui::OptionDef& option, platform::Key key)
{
    const auto action = platform::actionFromName(option.id);
    if (!action)
    {
        return {};
    }
    const std::vector<platform::Action> displaced = ui::assignKey(m_actions, *action, key, kAssignable);
    const std::string scheme = m_config.settings.get<std::string>("input.scheme", "classic");
    m_actions.writeTo(m_config.settings, scheme);
    for (const platform::Action a : kAssignable)
    {
        m_changedSettings.insert(std::format("bindings.{}.{}", scheme, platform::name(a)));
    }
    if (displaced.empty() || !m_gameUi)
    {
        return {};
    }
    std::string names;
    for (const platform::Action a : displaced)
    {
        names += (names.empty() ? "" : ", ") + std::string(m_gameUi->strings().get(actionLabelKey(a)));
    }
    return ui::UiText::key("UI_OPTIONS_KEY_TAKEN",
                           {{"key", std::string(platform::name(key))}, {"actions", names}})
        .resolve(m_gameUi->strings());
}

void Engine::openOptions()
{
    ui::OptionsScreen::Actions actions;
    actions.get = [this](const ui::OptionDef& option) { return optionValue(option); };
    actions.set = [this](const ui::OptionDef& option, const ui::OptionValue& value)
    { setOption(option, value); };
    actions.binding = [this](const ui::OptionDef& option) { return bindingText(option); };
    actions.assign = [this](const ui::OptionDef& option, platform::Key key)
    { return assignBinding(option, key); };
    actions.close = [this] { saveUserSettings(); };
    auto screen = std::make_unique<ui::OptionsScreen>(optionPages(), std::move(actions));
    screen->selectPage(std::exchange(m_optionsTab, usize{0}));
    m_screens.push(std::move(screen));
}

void Engine::saveUserSettings()
{
    if (m_changedSettings.empty() || m_config.userConfig.empty())
    {
        return;
    }
    // The user config as it is on disk: keys written by hand or by other parts of the game stay.
    Config user;
    if (fs::exists(m_config.userConfig))
    {
        if (auto loaded = Config::load(m_config.userConfig))
        {
            user = std::move(loaded).value();
        }
        else
        {
            G7_LOG_WARN("engine", "user config not read, writing it anew: {}", loaded.error().message);
        }
    }
    for (const std::string& key : m_changedSettings)
    {
        // Only what differs from the defaults is written; a value set back to its default goes.
        if (m_config.defaultSettings.sameValue(key, m_config.settings))
        {
            (void)user.erase(key);
        }
        else
        {
            user.copyValue(key, m_config.settings);
        }
    }
    if (auto saved = user.save(m_config.userConfig); !saved)
    {
        G7_LOG_ERROR("engine", "options not saved: {}", saved.error().message);
        return;
    }
    G7_LOG_INFO("engine", "options saved to {}", fs::toUtf8(m_config.userConfig));
    m_changedSettings.clear();
}
} // namespace g7
