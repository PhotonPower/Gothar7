// The game UI in the engine (M14, docs/modules/ui.md "Plan M14"): resources from the VFS, the canvas, the
// screen stack with its input (ui_* actions, stick, mouse), UI sounds, hot reload of the UI files, and
// drawing after the tonemapped scene and before ImGui.

#include <g7/core/Log.hpp>
#include <g7/runtime/Engine.hpp>
#include <g7/ui/SamplePage.hpp>

#include <algorithm>
#include <format>

namespace g7
{
namespace
{
/// UI units are pixels at this framebuffer height.
constexpr f32 kReferenceHeight = 1080.0f;
/// How often the UI files are checked for changes (hot reload).
constexpr f64 kReloadCheckSeconds = 0.5;
} // namespace

void Engine::initGameUi()
{
    auto canvas = ui::Canvas::create(m_device.get(), m_shaders.get());
    if (!canvas)
    {
        G7_LOG_ERROR("ui", "game UI canvas: {} (no game UI)", canvas.error().message);
        return;
    }
    m_canvas = std::move(canvas).value();
    m_navRepeat.delay = static_cast<f32>(m_config.settings.get<f64>("ui.nav_repeat_delay", 0.35));
    m_navRepeat.rate = static_cast<f32>(m_config.settings.get<f64>("ui.nav_repeat_rate", 0.08));
    const std::string language = m_config.settings.get<std::string>("ui.language", "de");
    auto resources = ui::UiResources::load([this](std::string_view path) { return m_vfs.read(path); },
                                           language, m_device.get());
    if (!resources)
    {
        G7_LOG_ERROR("ui", "game UI: {} (no game UI)", resources.error().message);
        return;
    }
    m_gameUi = std::move(resources).value();
    m_gameUiStamp.clear();
    m_gameUiReloadTimer = kReloadCheckSeconds;
    reloadGameUiIfChanged(0.0); // records the files' times
    if (m_config.uiSample)
    {
        m_screens.push(ui::makeSampleScreen());
    }
}

void Engine::reloadGameUiIfChanged(f64 realSeconds)
{
    if (!m_gameUi || !m_assets || !m_assets->hotReload())
    {
        return;
    }
    m_gameUiReloadTimer += realSeconds;
    if (m_gameUiReloadTimer < kReloadCheckSeconds)
    {
        return;
    }
    m_gameUiReloadTimer = 0.0;
    std::string stamp;
    for (const std::string& path : m_gameUi->files())
    {
        const auto disk = m_vfs.diskPath(path);
        if (!disk)
        {
            continue; // packed or missing: nothing to watch
        }
        const auto time = fs::lastWriteTime(*disk);
        stamp += std::format("{}={};", path, time ? time.value().time_since_epoch().count() : 0);
    }
    if (m_gameUiStamp.empty())
    {
        m_gameUiStamp = std::move(stamp);
        return;
    }
    if (stamp == m_gameUiStamp)
    {
        return;
    }
    m_gameUiStamp = std::move(stamp);
    const std::string language = m_config.settings.get<std::string>("ui.language", "de");
    auto resources = ui::UiResources::load([this](std::string_view path) { return m_vfs.read(path); },
                                           language, m_device.get());
    if (!resources)
    {
        G7_LOG_ERROR("ui", "game UI not reloaded: {} (the previous version stays)",
                     resources.error().message);
        return;
    }
    m_gameUi = std::move(resources).value();
    G7_LOG_INFO("ui", "game UI reloaded");
}

void Engine::updateGameUi(f64 realSeconds, bool allowMouse, bool allowKeyboard)
{
    reloadGameUiIfChanged(realSeconds);
    if (!m_gameUi || !m_window)
    {
        return;
    }
    const auto size = m_window->size();
    const auto pixels = m_window->pixelSize();
    const Vec2 pointerScale(
        size.width > 0 ? static_cast<f32>(pixels.width) / static_cast<f32>(size.width) : 1.0f,
        size.height > 0 ? static_cast<f32>(pixels.height) / static_cast<f32>(size.height) : 1.0f);
    ui::UiInput input =
        ui::buildUiInput(m_input, m_actions, m_navRepeat, static_cast<f32>(realSeconds), pointerScale);
    if (!allowKeyboard)
    {
        // The debug UI has the keyboard (a focused text field): no navigation, choosing or going back.
        input.nav = ui::NavDir::None;
        input.accept = false;
        input.back = false;
        input.prevTab = false;
        input.nextTab = false;
    }
    if (!allowMouse)
    {
        input.pointerMoved = false;
        input.click = false;
        input.wheel = 0.0f;
    }
    // Without a framebuffer (no renderer) the layout assumes 1920 x 1080.
    const f32 width = pixels.width > 0 ? static_cast<f32>(pixels.width) : 1920.0f;
    const f32 height = pixels.height > 0 ? static_cast<f32>(pixels.height) : 1080.0f;
    const f32 userScale =
        std::clamp(static_cast<f32>(m_config.settings.get<f64>("ui.scale", 1.0)), 0.5f, 2.0f);
    m_screens.update(input, static_cast<f32>(realSeconds), ui::Rect{Vec2(0.0f), Vec2(width, height)},
                     height / kReferenceHeight * userScale, &m_gameUi->screenLayout());
    for (const std::string& event : m_screens.takeEvents())
    {
        const std::string_view sound = m_gameUi->style().sound(event);
        if (!sound.empty())
        {
            (void)playSound(sound);
        }
    }
}

void Engine::drawGameUi(u32 width, u32 height)
{
    if (!m_gameUi || m_screens.empty() || width == 0 || height == 0)
    {
        return;
    }
    const f32 userScale =
        std::clamp(static_cast<f32>(m_config.settings.get<f64>("ui.scale", 1.0)), 0.5f, 2.0f);
    const f32 scale = static_cast<f32>(height) / kReferenceHeight * userScale;
    m_canvas.begin(Vec2(static_cast<f32>(width), static_cast<f32>(height)), scale);
    m_screens.draw(m_canvas, *m_gameUi);
    m_canvas.end(m_device.get());
}
} // namespace g7
