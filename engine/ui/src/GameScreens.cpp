#include <g7/ui/GameScreens.hpp>
#include <g7/ui/UiResources.hpp>

#include <algorithm>
#include <string>

namespace g7::ui
{
namespace
{
/// The rows of a list from plain texts (game data, already in the player's language).
void setRows(List& list, const std::vector<std::string>& texts)
{
    list.rows.clear();
    for (const std::string& text : texts)
    {
        list.rows.push_back(UiText::literal(text));
    }
}
} // namespace

// --- Dialogue ---

DialogScreen::DialogScreen(Actions actions) : m_actions(std::move(actions))
{
    Panel& panel = root().add<Panel>("parchment");
    panel.layoutKey = "dialog.box";
    panel.box = {Anchor::Bottom, Vec2(0.0f, -40.0f), Vec2(1100.0f, 260.0f)};
    m_panel = &panel;
    List& list = panel.add<List>("dialog", 40.0f);
    list.layoutKey = "dialog.options";
    list.box = {Anchor::Center, Vec2(0.0f), Vec2(-80.0f, -48.0f)};
    list.highlightColor = "ink_highlight";
    list.onAccept = [this](usize option)
    {
        if (m_actions.choose)
        {
            m_actions.choose(option);
        }
    };
    m_list = &list;
}

void DialogScreen::onUpdate(f32)
{
    // The selection the player moved last frame goes back to the engine first.
    if (m_view.menu && m_list->selected() != m_reported && m_actions.select)
    {
        m_reported = m_list->selected();
        m_actions.select(m_reported);
    }
    m_view = m_actions.view ? m_actions.view() : DialogView{};
    const bool showMenu = m_view.menu && !m_view.hidden && !m_view.options.empty();
    m_panel->visible = showMenu;
    // As high as the options (at most 6 rows; more scroll), plus the parchment's margins.
    const usize rows = std::min<usize>(std::max<usize>(m_view.options.size(), 1), 6);
    m_panel->heightOverride = static_cast<f32>(rows) * m_list->rowHeight + 56.0f;
    if (m_view.options != m_shown)
    {
        m_shown = m_view.options;
        setRows(*m_list, m_shown);
        m_list->select(m_view.selected);
        m_reported = m_list->selected();
    }
}

bool DialogScreen::onAccept()
{
    if (m_view.hidden || m_view.menu || !m_actions.skip)
    {
        return false;
    }
    m_actions.skip();
    return true;
}

// --- Document ---

DocumentScreen::DocumentScreen(std::string title, std::string text, std::function<void()> onClose)
    : m_title(std::move(title)), m_text(std::move(text)), m_onClose(std::move(onClose))
{
    Panel& page = root().add<Panel>("parchment");
    page.layoutKey = "document.page";
    page.box = {Anchor::Center, Vec2(0.0f), Vec2(900.0f, 680.0f)};
    Label& heading = page.add<Label>("document_title", UiText::literal(m_title), Align::Center);
    heading.layoutKey = "document.title";
    heading.box = {Anchor::Top, Vec2(0.0f, 48.0f), Vec2(-120.0f, 50.0f)};
    Label& body = page.add<Label>("document", UiText::literal(m_text));
    body.layoutKey = "document.text";
    body.box = {Anchor::Top, Vec2(0.0f, 112.0f), Vec2(-140.0f, -170.0f)};
    Label& hint = page.add<Label>("document_hint", UiText::key("UI_DOCUMENT_CLOSE"), Align::Center);
    hint.layoutKey = "document.hint";
    hint.box = {Anchor::Bottom, Vec2(0.0f, -36.0f), Vec2(-120.0f, 26.0f)};
}

bool DocumentScreen::onAccept()
{
    return onBack();
}

bool DocumentScreen::onBack()
{
    if (m_onClose)
    {
        m_onClose();
    }
    close();
    return true;
}

// --- Choice at a mob ---

ChoiceScreen::ChoiceScreen(Actions actions) : m_actions(std::move(actions))
{
    Panel& panel = root().add<Panel>("panel");
    panel.layoutKey = "choice.box";
    panel.box = {Anchor::Right, Vec2(-80.0f, 0.0f), Vec2(520.0f, 420.0f)};
    m_title = &panel.add<Label>("title", UiText::literal(""), Align::Center);
    m_title->layoutKey = "choice.title";
    m_title->box = {Anchor::Top, Vec2(0.0f, 36.0f), Vec2(-80.0f, 50.0f)};
    List& list = panel.add<List>("body", 38.0f);
    list.layoutKey = "choice.options";
    list.box = {Anchor::Top, Vec2(0.0f, 100.0f), Vec2(-80.0f, 220.0f)};
    list.onAccept = [this](usize option)
    {
        if (m_actions.choose)
        {
            m_actions.choose(option);
        }
    };
    m_list = &list;
    m_message = &panel.add<Label>("small", UiText::literal(""), Align::Center);
    m_message->layoutKey = "choice.message";
    m_message->box = {Anchor::Bottom, Vec2(0.0f, -64.0f), Vec2(-80.0f, 50.0f)};
    Label& hint = panel.add<Label>("small", UiText::key("UI_CHOICE_LEAVE"), Align::Center);
    hint.layoutKey = "choice.hint";
    hint.box = {Anchor::Bottom, Vec2(0.0f, -30.0f), Vec2(-80.0f, 26.0f)};
}

void ChoiceScreen::onUpdate(f32)
{
    const ChoiceView view = m_actions.view ? m_actions.view() : ChoiceView{};
    m_title->text = UiText::literal(view.title);
    m_message->text = UiText::literal(view.message);
    if (view.options != m_shown)
    {
        m_shown = view.options;
        setRows(*m_list, m_shown);
        m_list->select(0);
    }
}

bool ChoiceScreen::onBack()
{
    if (m_actions.leave)
    {
        m_actions.leave();
    }
    return true; // the engine closes the screen when the hero has left the mob
}

// --- Lockpicking ---

LockpickScreen::LockpickScreen(Actions actions) : m_actions(std::move(actions))
{
    Panel& panel = root().add<Panel>("panel");
    panel.layoutKey = "lockpick.box";
    panel.box = {Anchor::Center, Vec2(0.0f, 200.0f), Vec2(640.0f, 300.0f)};
    m_title = &panel.add<Label>("title", UiText::literal(""), Align::Center);
    m_title->layoutKey = "lockpick.title";
    m_title->box = {Anchor::Top, Vec2(0.0f, 30.0f), Vec2(-80.0f, 50.0f)};
    m_steps = &panel.add<Grid>(0, 12, 40.0f, 8.0f);
    m_steps->layoutKey = "lockpick.steps";
    m_steps->box = {Anchor::Top, Vec2(0.0f, 96.0f), Vec2(-80.0f, 44.0f)};
    m_steps->interactive = false;
    m_steps->drawCell = [this](const DrawContext& context, const Rect& rect, usize index, bool)
    {
        const std::string_view frame = index < m_view.progress ? "cell_selected" : "cell";
        context.canvas.frame(rect, context.ui.frame(frame, context.scale()), context.ui.frameTint(frame));
    };
    m_picks = &panel.add<Label>("body", UiText::literal(""), Align::Center);
    m_picks->layoutKey = "lockpick.picks";
    m_picks->box = {Anchor::Top, Vec2(0.0f, 152.0f), Vec2(-80.0f, 32.0f)};
    m_result = &panel.add<Label>("small", UiText::literal(""), Align::Center);
    m_result->layoutKey = "lockpick.result";
    m_result->box = {Anchor::Top, Vec2(0.0f, 190.0f), Vec2(-80.0f, 30.0f)};
    Label& hint = panel.add<Label>("small", UiText::key("UI_LOCKPICK_HINT"), Align::Center);
    hint.layoutKey = "lockpick.hint";
    hint.box = {Anchor::Bottom, Vec2(0.0f, -26.0f), Vec2(-80.0f, 26.0f)};
}

void LockpickScreen::onUpdate(f32)
{
    m_view = m_actions.view ? m_actions.view() : LockpickView{};
    m_title->text = UiText::literal(m_view.title);
    m_picks->text = UiText::key("UI_LOCKPICK_PICKS", {{"n", std::to_string(m_view.picks)}});
    m_result->text = UiText::literal(m_view.result);
    m_steps->count = m_view.length;
    m_steps->columns = std::max<usize>(1, m_view.length);
}

bool LockpickScreen::onDirection(NavDir dir)
{
    if ((dir != NavDir::Left && dir != NavDir::Right) || !m_actions.turn)
    {
        return false;
    }
    m_actions.turn(dir == NavDir::Left);
    return true;
}

bool LockpickScreen::onBack()
{
    if (m_actions.leave)
    {
        m_actions.leave();
    }
    return true;
}
} // namespace g7::ui
