#include <g7/ui/SamplePage.hpp>
#include <g7/ui/UiResources.hpp>

#include <string>

namespace g7::ui
{
namespace
{
constexpr usize kRows = 12;
constexpr usize kCells = 17;

/// A cell of the sample grid: its frame and its number.
void drawSampleCell(const DrawContext& context, const Rect& rect, usize index, bool selected)
{
    const std::string_view frame = selected ? "cell_selected" : "cell";
    context.canvas.frame(rect, context.ui.frame(frame, context.scale()), context.ui.frameTint(frame));
    const TextLayout layout = context.ui.layout("small", std::to_string(index + 1), context.scale(),
                                                {.wrapWidth = rect.width(), .align = Align::Center});
    context.canvas.text(Vec2(rect.min.x, rect.min.y + (rect.height() - layout.size.y) * 0.5f), layout,
                        context.ui.textStyle("small", context.scale()));
}
} // namespace

SampleScreen::SampleScreen()
{
    Widget& top = root();

    Panel& panel = top.add<Panel>("panel");
    panel.layoutKey = "sample.panel";
    panel.box = {Anchor::Center, Vec2(0.0f), Vec2(1100.0f, 640.0f)};

    Label& title = panel.add<Label>("title", UiText::key("UI_SAMPLE_TITLE"), Align::Center);
    title.layoutKey = "sample.title";
    title.box = {Anchor::Top, Vec2(0.0f, 30.0f), Vec2(-96.0f, 50.0f)};

    Label& body = panel.add<Label>("body", UiText::key("UI_SAMPLE_BODY"));
    body.layoutKey = "sample.body";
    body.box = {Anchor::Top, Vec2(0.0f, 92.0f), Vec2(-96.0f, 70.0f)};

    Label& money =
        panel.add<Label>("small", UiText::key("UI_MONEY", {{"amount", formatNumber(1234567, "de")}}));
    money.layoutKey = "sample.small";
    money.box = {Anchor::Top, Vec2(0.0f, 170.0f), Vec2(-96.0f, 26.0f)};

    List& rows = panel.add<List>("body", 36.0f);
    rows.layoutKey = "sample.list";
    rows.box = {Anchor::TopLeft, Vec2(48.0f, 210.0f), Vec2(440.0f, 290.0f)};
    for (usize i = 0; i < kRows; ++i)
    {
        rows.rows.push_back(UiText::key("UI_SAMPLE_ROW", {{"n", std::to_string(i + 1)}}));
    }
    m_list = &rows;

    Grid& cells = panel.add<Grid>(kCells, 5, 80.0f, 6.0f);
    cells.layoutKey = "sample.grid";
    cells.box = {Anchor::TopRight, Vec2(-48.0f, 210.0f), Vec2(424.0f, 290.0f)};
    cells.drawCell = drawSampleCell;
    m_grid = &cells;

    Label& chosen = panel.add<Label>("small", UiText::literal(""));
    chosen.layoutKey = "sample.chosen";
    chosen.box = {Anchor::BottomLeft, Vec2(48.0f, -36.0f), Vec2(500.0f, 28.0f)};
    m_chosen = &chosen;
    rows.onAccept = [&chosen](usize row)
    { chosen.text = UiText::key("UI_SAMPLE_CHOSEN_ROW", {{"n", std::to_string(row + 1)}}); };
    cells.onAccept = [&chosen](usize cell)
    { chosen.text = UiText::key("UI_SAMPLE_CHOSEN_CELL", {{"n", std::to_string(cell + 1)}}); };

    Stack& buttons = panel.add<Stack>(Stack::Direction::Horizontal, 16.0f);
    buttons.layoutKey = "sample.buttons";
    buttons.box = {Anchor::BottomRight, Vec2(-48.0f, -28.0f), Vec2(416.0f, 44.0f)};
    buttons.add<Button>(UiText::key("UI_OK"), [&chosen] { chosen.text = UiText::key("UI_OK"); });
    buttons.add<Button>(UiText::key("UI_CLOSE"), [this] { close(); });

    // The HUD as in Gothic 1 (F3), a screen message (F7) and a subtitle (F6) in their places.
    Bar& hp = top.add<Bar>("bar_hp", "hp", 0.7f);
    hp.layoutKey = "hud.hp";
    hp.box = {Anchor::BottomLeft, Vec2(32.0f, -32.0f), Vec2(260.0f, 24.0f)};
    Bar& mana = top.add<Bar>("bar_mana", "mana", 0.4f);
    mana.layoutKey = "hud.mana";
    mana.box = {Anchor::BottomRight, Vec2(-32.0f, -32.0f), Vec2(260.0f, 24.0f)};
    Label& message = top.add<Label>("message", UiText::key("UI_XP_GAINED", {{"xp", "50"}}), Align::Center);
    message.layoutKey = "hud.message";
    message.box = {Anchor::Top, Vec2(0.0f, 60.0f), Vec2(1200.0f, 40.0f)};
    Label& subtitle = top.add<Label>("subtitle", UiText::key("UI_SAMPLE_SUBTITLE"), Align::Center);
    subtitle.layoutKey = "hud.subtitle";
    subtitle.box = {Anchor::Bottom, Vec2(0.0f, -110.0f), Vec2(1200.0f, 80.0f)};
    subtitle.maxLines = 2;
}

const UiText& SampleScreen::chosen() const noexcept
{
    return m_chosen->text;
}

std::unique_ptr<Screen> makeSampleScreen()
{
    return std::make_unique<SampleScreen>();
}
} // namespace g7::ui
