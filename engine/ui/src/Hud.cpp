#include <g7/ui/Hud.hpp>
#include <g7/ui/Layout.hpp>
#include <g7/ui/UiResources.hpp>

#include <cmath>

namespace g7::ui
{
namespace
{
constexpr f32 kFadeSeconds = 0.5f; ///< messages fade out over their last half second
constexpr Vec2 kBarInset(7.0f, 5.0f);

LayoutBox boxOf(const Layout* layout, std::string_view key, const LayoutBox& fallback)
{
    return layout != nullptr ? layout->box(key, fallback) : fallback;
}

/// A HUD bar as figuren's frames expect it: dark back, the coloured fill frame, the rim over it.
void drawBar(Canvas& canvas, const UiResources& ui, const Rect& rect, f32 fraction, std::string_view fill,
             std::string_view color)
{
    const Style& style = ui.style();
    const f32 s = canvas.uiScale();
    Rect area{rect.min + kBarInset * s, rect.max - kBarInset * s};
    canvas.rect(area, style.color("bar_back"));
    area.max.x = area.min.x + std::max(0.0f, area.width()) * std::clamp(fraction, 0.0f, 1.0f);
    if (style.frames.contains(fill))
    {
        canvas.frame(area, ui.frame(fill, s), ui.frameTint(fill));
    }
    else
    {
        canvas.rect(area, style.color(color));
    }
    canvas.frame(rect, ui.frame("bar", s), ui.frameTint("bar"));
}
} // namespace

void MessageQueue::push(std::string text, f32 seconds)
{
    m_messages.push_back({std::move(text), seconds});
    while (m_messages.size() > kMaxShown)
    {
        m_messages.pop_front();
    }
}

void MessageQueue::update(f32 deltaSeconds)
{
    for (Message& m : m_messages)
    {
        m.remaining -= deltaSeconds;
    }
    while (!m_messages.empty() && m_messages.front().remaining <= 0.0f)
    {
        m_messages.pop_front();
    }
    std::erase_if(m_messages, [](const Message& m) { return m.remaining <= 0.0f; });
}

void SubtitleQueue::push(std::string speaker, std::string text, f32 seconds)
{
    std::erase_if(m_lines, [&](const Line& l) { return l.speaker == speaker; });
    m_lines.push_back({std::move(speaker), std::move(text), seconds});
    while (m_lines.size() > kMaxShown)
    {
        m_lines.pop_front();
    }
}

void SubtitleQueue::update(f32 deltaSeconds)
{
    for (Line& l : m_lines)
    {
        l.remaining -= deltaSeconds;
    }
    std::erase_if(m_lines, [](const Line& l) { return l.remaining <= 0.0f; });
}

f32 readingSeconds(std::string_view text) noexcept
{
    usize characters = 0;
    for (const char c : text)
    {
        characters += (static_cast<u8>(c) & 0xC0) != 0x80 ? 1 : 0; // UTF-8 lead bytes
    }
    return std::clamp(1.5f + 0.06f * static_cast<f32>(characters), 2.0f, 10.0f);
}

void Hud::update(const HudModel& model, const MessageQueue& messages,
                 std::span<const SubtitleQueue::Line> subtitles, const Rect& area, f32 scale,
                 const Layout* layout)
{
    m_model = model;
    m_scale = scale;
    m_messages.assign(messages.messages().begin(), messages.messages().end());
    const usize shown = std::min(subtitles.size(), SubtitleQueue::kMaxShown);
    m_subtitles.assign(subtitles.end() - static_cast<std::ptrdiff_t>(shown), subtitles.end());
    const auto at = [&](std::string_view key, const LayoutBox& fallback)
    { return place(boxOf(layout, key, fallback), area, scale); };
    m_health = at("hud.hp", {Anchor::BottomLeft, Vec2(32.0f, -32.0f), Vec2(260.0f, 24.0f)});
    m_mana = at("hud.mana", {Anchor::BottomRight, Vec2(-32.0f, -32.0f), Vec2(260.0f, 24.0f)});
    m_enemy = at("hud.enemy", {Anchor::Top, Vec2(0.0f, 24.0f), Vec2(360.0f, 24.0f)});
    m_air = at("hud.air", {Anchor::BottomLeft, Vec2(32.0f, -66.0f), Vec2(260.0f, 24.0f)});
    m_message = at("hud.message", {Anchor::Top, Vec2(0.0f, 60.0f), Vec2(1200.0f, 40.0f)});
    m_subtitle = at("hud.subtitle", {Anchor::Bottom, Vec2(0.0f, -110.0f), Vec2(1200.0f, 80.0f)});
}

void Hud::draw(Canvas& canvas, const UiResources& ui) const
{
    const f32 s = m_scale;
    const Style& style = ui.style();
    if (m_model.visible)
    {
        drawBar(canvas, ui, m_health, m_model.health.fraction(), "bar_hp", "hp");
        if (m_model.mana)
        {
            drawBar(canvas, ui, m_mana, m_model.mana->fraction(), "bar_mana", "mana");
        }
        if (m_model.air)
        {
            drawBar(canvas, ui, m_air, *m_model.air, "bar_air", "air");
        }
        if (m_model.enemy)
        {
            drawBar(canvas, ui, m_enemy, m_model.enemy->health.fraction(), "bar_enemy", "hp");
            const TextLayout name =
                ui.layout("small", m_model.enemy->name, s,
                          {.wrapWidth = m_enemy.width() * 2.0f, .maxLines = 1, .align = Align::Center});
            canvas.text(Vec2(m_enemy.min.x - m_enemy.width() * 0.5f, m_enemy.max.y + 2.0f * s), name,
                        ui.textStyle("small", s));
        }
        if (m_model.focus && !m_model.focus->name.empty())
        {
            const TextLayout name = ui.layout("focus", m_model.focus->name, s, {.maxLines = 1});
            canvas.text(m_model.focus->point - Vec2(name.size.x * 0.5f, name.size.y), name,
                        ui.textStyle("focus", s));
        }
    }

    // Messages top centre (F7), the oldest on top, fading out.
    f32 y = m_message.min.y;
    for (const MessageQueue::Message& m : m_messages)
    {
        const TextLayout line = ui.layout(
            "message", m.text, s, {.wrapWidth = m_message.width(), .maxLines = 2, .align = Align::Center});
        const f32 opacity = std::clamp(m.remaining / kFadeSeconds, 0.0f, 1.0f);
        canvas.text(Vec2(m_message.min.x, y), line, ui.textStyle("message", s).faded(opacity));
        y += line.size.y + 4.0f * s;
    }

    // Subtitles bottom centre (F6): the speaker's name over the text on a soft backdrop, the newest at the
    // bottom.
    const Vec2 pad = Vec2(28.0f, 12.0f) * s;
    f32 bottom = m_subtitle.max.y;
    for (auto it = m_subtitles.rbegin(); it != m_subtitles.rend(); ++it)
    {
        const f32 wrap = std::max(1.0f, m_subtitle.width() - 2.0f * pad.x);
        const TextLayout text =
            ui.layout("subtitle", it->text, s, {.wrapWidth = wrap, .maxLines = 3, .align = Align::Center});
        const TextLayout name = ui.layout("subtitle_name", it->speaker, s,
                                          {.wrapWidth = wrap, .maxLines = 1, .align = Align::Center});
        const f32 width = std::max(text.size.x, name.size.x) + 2.0f * pad.x;
        const f32 height = name.size.y + text.size.y + 2.0f * pad.y;
        const f32 centre = (m_subtitle.min.x + m_subtitle.max.x) * 0.5f;
        const Rect box{Vec2(centre - width * 0.5f, bottom - height), Vec2(centre + width * 0.5f, bottom)};
        if (style.frames.contains("subtitle"))
        {
            canvas.frame(box, ui.frame("subtitle", s), ui.frameTint("subtitle"));
        }
        const f32 textLeft = centre - wrap * 0.5f;
        canvas.text(Vec2(textLeft, box.min.y + pad.y), name, ui.textStyle("subtitle_name", s));
        canvas.text(Vec2(textLeft, box.min.y + pad.y + name.size.y), text, ui.textStyle("subtitle", s));
        bottom = box.min.y - 6.0f * s;
    }
}
} // namespace g7::ui
