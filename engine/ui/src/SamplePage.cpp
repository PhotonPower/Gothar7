#include <g7/ui/Canvas.hpp>
#include <g7/ui/SamplePage.hpp>
#include <g7/ui/UiResources.hpp>

#include <string>

namespace g7::ui
{
namespace
{
/// A bar like the HUD's: dark back, coloured fill, frame on top.
void bar(Canvas& canvas, const UiResources& ui, const Rect& rect, f32 fraction, Color fill)
{
    canvas.rect(rect, ui.style().color("bar_back"));
    const f32 inset = 3.0f * canvas.uiScale();
    Rect inner{rect.min + Vec2(inset), rect.max - Vec2(inset)};
    inner.max.x = inner.min.x + inner.width() * fraction;
    canvas.rect(inner, fill);
}

void text(Canvas& canvas, const UiResources& ui, std::string_view role, std::string_view utf8, Vec2 position,
          TextOptions options = {})
{
    const f32 s = canvas.uiScale();
    const TextLayout layout = ui.layout(role, utf8, s, options);
    canvas.text(position, layout, ui.textStyle(role, s));
}
} // namespace

void drawSamplePage(Canvas& canvas, const UiResources& ui)
{
    const f32 s = canvas.uiScale();
    const Vec2 screen = canvas.pixels();
    const Strings& strings = ui.strings();

    // Main panel in the middle (1100 x 600 UI units).
    const Vec2 panelSize = Vec2(1100.0f, 600.0f) * s;
    const Rect panel = Rect::fromSize((screen - panelSize) * 0.5f, panelSize);
    canvas.frame(panel, ui.frame("panel", s), ui.frameTint("panel"));
    const f32 pad = 48.0f * s;
    const f32 width = panel.width() - 2.0f * pad;
    Vec2 at = panel.min + Vec2(pad, 36.0f * s);
    text(canvas, ui, "title", strings.get("UI_SAMPLE_TITLE"), at,
         {.wrapWidth = width, .align = Align::Center});
    at.y += ui.textSize("title", s) * 1.6f;
    const TextLayout body = ui.layout("body", strings.get("UI_SAMPLE_BODY"), s, {.wrapWidth = width});
    canvas.text(at, body, ui.textStyle("body", s));
    at.y += body.size.y + 16.0f * s;
    text(canvas, ui, "small",
         std::string(strings.get("UI_SAMPLE_SMALL")) + " - " +
             strings.format("UI_MONEY", {{"amount", formatNumber(1234567, strings.language())}}),
         at);
    at.y += ui.textSize("small", s) * 1.8f;

    // A letter on parchment, clipped to its box.
    const Rect letter = Rect::fromSize(at, Vec2(width, panel.max.y - pad - at.y));
    canvas.frame(letter, ui.frame("parchment", s), ui.frameTint("parchment"));
    canvas.pushClip(letter);
    text(canvas, ui, "document", strings.get("UI_SAMPLE_DOCUMENT"), letter.min + Vec2(28.0f * s, 22.0f * s),
         {.wrapWidth = letter.width() - 56.0f * s});
    canvas.popClip();

    // HUD bars as in Gothic 1 (F3): health bottom left, mana bottom right.
    const Vec2 barSize = Vec2(260.0f, 22.0f) * s;
    const f32 margin = 32.0f * s;
    bar(canvas, ui, Rect::fromSize(Vec2(margin, screen.y - margin - barSize.y), barSize), 0.7f,
        ui.style().color("hp"));
    bar(canvas, ui,
        Rect::fromSize(Vec2(screen.x - margin - barSize.x, screen.y - margin - barSize.y), barSize), 0.4f,
        ui.style().color("mana"));

    // Screen message top centre (F7) and a subtitle bottom centre (F6).
    text(canvas, ui, "message", strings.format("UI_XP_GAINED", {{"xp", "50"}}), Vec2(0.0f, 60.0f * s),
         {.wrapWidth = screen.x, .align = Align::Center});
    const f32 subtitleWidth = 1200.0f * s;
    text(canvas, ui, "subtitle", strings.get("UI_SAMPLE_SUBTITLE"),
         Vec2((screen.x - subtitleWidth) * 0.5f, screen.y - 140.0f * s),
         {.wrapWidth = subtitleWidth, .maxLines = 2, .align = Align::Center});
}
} // namespace g7::ui
