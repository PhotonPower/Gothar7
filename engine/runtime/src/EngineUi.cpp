// The game UI in the engine (M14, docs/modules/ui.md "Plan M14"): resources from the VFS, the canvas, and
// drawing after the tonemapped scene and before ImGui. Part A: the sample page (--ui-sample).

#include <g7/core/Log.hpp>
#include <g7/runtime/Engine.hpp>
#include <g7/ui/SamplePage.hpp>

#include <algorithm>

namespace g7
{
namespace
{
/// UI units are pixels at this framebuffer height.
constexpr f32 kReferenceHeight = 1080.0f;
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
    const std::string language = m_config.settings.get<std::string>("ui.language", "de");
    auto resources = ui::UiResources::load([this](std::string_view path) { return m_vfs.read(path); },
                                           language, m_device.get());
    if (!resources)
    {
        G7_LOG_ERROR("ui", "game UI: {} (no game UI)", resources.error().message);
        return;
    }
    m_gameUi = std::move(resources).value();
}

void Engine::drawGameUi(u32 width, u32 height)
{
    if (!m_gameUi || !m_config.uiSample || width == 0 || height == 0)
    {
        return;
    }
    const f32 userScale =
        std::clamp(static_cast<f32>(m_config.settings.get<f64>("ui.scale", 1.0)), 0.5f, 2.0f);
    const f32 scale = static_cast<f32>(height) / kReferenceHeight * userScale;
    m_canvas.begin(Vec2(static_cast<f32>(width), static_cast<f32>(height)), scale);
    ui::drawSamplePage(m_canvas, *m_gameUi);
    m_canvas.end(m_device.get());
}
} // namespace g7
