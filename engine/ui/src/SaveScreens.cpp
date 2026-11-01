#include <g7/ui/SaveScreens.hpp>
#include <g7/ui/UiResources.hpp>

#include <algorithm>

namespace g7::ui
{
/// The selected slot's thumbnail, fitted into its box (aspect kept).
class SaveThumbnail : public Widget
{
public:
    explicit SaveThumbnail(const SaveScreen& screen) : m_screen(screen) {}
    [[nodiscard]] Rect fitted() const noexcept
    {
        const Vec2 size = m_screen.m_view.thumbnailSize;
        if (size.x <= 0.0f || size.y <= 0.0f)
        {
            return m_rect;
        }
        const f32 scale = std::min(m_rect.width() / size.x, m_rect.height() / size.y);
        const Vec2 drawn = size * scale;
        const Vec2 min = m_rect.min + (m_rect.size() - drawn) * 0.5f;
        return {min, min + drawn};
    }

protected:
    void drawSelf(const DrawContext& context) const override
    {
        if (m_screen.m_view.thumbnail != nullptr)
        {
            context.canvas.image(fitted(), m_screen.m_view.thumbnail, {Vec2(0.0f), Vec2(1.0f)}, Color{});
        }
    }

private:
    const SaveScreen& m_screen;
};

SaveScreen::SaveScreen(Mode mode, Actions actions) : m_mode(mode), m_actions(std::move(actions))
{
    Panel& panel = root().add<Panel>("panel");
    panel.layoutKey = "save.box";
    panel.box = {Anchor::Center, Vec2(0.0f), Vec2(1400.0f, 800.0f)};
    Label& title = panel.add<Label>(
        "title", UiText::key(mode == Mode::Save ? "UI_MENU_SAVE" : "UI_MENU_LOAD"), Align::Center);
    title.layoutKey = "save.title";
    title.box = {Anchor::Top, Vec2(0.0f, 30.0f), Vec2(-80.0f, 56.0f)};
    List& list = panel.add<List>("body", 40.0f);
    list.layoutKey = "save.list";
    list.box = {Anchor::TopLeft, Vec2(50.0f, 110.0f), Vec2(820.0f, 600.0f)};
    list.onAccept = [this](usize row) { take(row); };
    m_list = &list;
    SaveThumbnail& picture = panel.add<SaveThumbnail>(*this);
    picture.layoutKey = "save.thumbnail";
    picture.box = {Anchor::TopRight, Vec2(-50.0f, 110.0f), Vec2(440.0f, 248.0f)};
    m_picture = &picture;
    Label& message = panel.add<Label>("small", UiText::literal(""), Align::Center);
    message.layoutKey = "save.message";
    message.box = {Anchor::Bottom, Vec2(0.0f, -28.0f), Vec2(-80.0f, 64.0f)};
    message.maxLines = 2;
    m_message = &message;
    setFocus(&list);
}

Rect SaveScreen::thumbnailRect() const noexcept
{
    return static_cast<const SaveThumbnail*>(m_picture)->fitted();
}

void SaveScreen::onUpdate(f32)
{
    m_view = m_actions.view ? m_actions.view() : SaveScreenView{};
    m_list->rows.clear();
    m_list->values.clear();
    m_list->dimmed.clear();
    for (const SaveSlotRow& row : m_view.rows)
    {
        m_list->rows.push_back(UiText::literal(row.title));
        m_list->values.push_back(UiText::literal(row.detail));
        m_list->dimmed.push_back(!row.usable);
    }
    if (m_actions.select && !m_list->rows.empty() && m_reported != m_list->selected())
    {
        m_reported = m_list->selected();
        m_actions.select(*m_reported); // the thumbnail follows (next frame)
        m_asking.clear();              // another row: the question is off
        m_askingRow.reset();
    }
    m_message->text = UiText::literal(m_asking.empty() ? m_view.message : m_asking);
}

void SaveScreen::take(usize row)
{
    if (row >= m_view.rows.size() || !m_view.rows[row].usable)
    {
        return;
    }
    if (m_askingRow != row)
    {
        std::string question = m_actions.question ? m_actions.question(row) : std::string();
        if (!question.empty())
        {
            m_asking = std::move(question);
            m_askingRow = row;
            return;
        }
    }
    m_asking.clear();
    m_askingRow.reset();
    if (m_actions.choose)
    {
        m_actions.choose(row);
    }
}

bool SaveScreen::onBack()
{
    if (m_askingRow)
    {
        m_asking.clear(); // "no" to the question
        m_askingRow.reset();
        return true;
    }
    if (m_actions.close)
    {
        m_actions.close();
    }
    close();
    return true;
}
} // namespace g7::ui
