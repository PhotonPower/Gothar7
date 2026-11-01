#include <g7/ui/JournalScreens.hpp>
#include <g7/ui/UiResources.hpp>

#include <algorithm>
#include <array>
#include <string>

namespace g7::ui
{
namespace
{
std::string joined(const std::vector<std::pair<std::string, std::string>>& rows, bool values)
{
    std::string out;
    for (const auto& [label, value] : rows)
    {
        if (!out.empty())
        {
            out += '\n';
        }
        out += values ? value : label;
    }
    return out;
}

void drawFrame(const DrawContext& context, const Rect& rect, std::string_view frame)
{
    context.canvas.frame(rect, context.ui.frame(frame, context.scale()), context.ui.frameTint(frame));
}

constexpr std::array<std::string_view, 4> kDiarySections = {"UI_DIARY_RUNNING", "UI_DIARY_DONE",
                                                            "UI_DIARY_FAILED", "UI_DIARY_NOTES"};
} // namespace

// --- TextView ---

bool TextView::scrollBy(f32 lines)
{
    const f32 before = m_offset;
    const f32 max = std::max(0.0f, m_contentHeight - m_rect.height());
    m_offset = std::clamp(m_offset + lines * m_lineHeight, 0.0f, max);
    return m_offset != before;
}

bool TextView::navigate(NavDir dir)
{
    if (dir == NavDir::Up)
    {
        return scrollBy(-1.0f);
    }
    if (dir == NavDir::Down)
    {
        return scrollBy(1.0f);
    }
    return false;
}

bool TextView::scroll(f32 steps)
{
    return scrollBy(-steps * 3.0f);
}

void TextView::drawSelf(const DrawContext& context) const
{
    const f32 s = context.scale();
    const TextLayout layout = context.ui.layout(role, text, s, {.wrapWidth = std::max(1.0f, m_rect.width())});
    m_contentHeight = layout.size.y;
    if (layout.lines > 0)
    {
        m_lineHeight = layout.size.y / static_cast<f32>(layout.lines);
    }
    const f32 offset = std::min(m_offset, std::max(0.0f, m_contentHeight - m_rect.height()));
    context.canvas.pushClip(m_rect);
    context.canvas.text(Vec2(m_rect.min.x, m_rect.min.y - offset), layout, context.ui.textStyle(role, s));
    context.canvas.popClip();
}

// --- Character sheet ---

StatusScreen::StatusScreen(std::function<StatusView()> view, std::function<void()> onClose)
    : m_source(std::move(view)), m_onClose(std::move(onClose))
{
    Panel& panel = root().add<Panel>("panel");
    panel.layoutKey = "status.panel";
    panel.box = {Anchor::Center, Vec2(0.0f), Vec2(1100.0f, 720.0f)};
    m_title = &panel.add<Label>("title", UiText::literal(""), Align::Center);
    m_title->layoutKey = "status.title";
    m_title->box = {Anchor::Top, Vec2(0.0f, 30.0f), Vec2(-80.0f, 50.0f)};
    const std::array<std::pair<const char*, const char*>, 4> columns = {
        {{"status.general", "UI_STATUS_GENERAL"},
         {"status.attributes", "UI_STATUS_ATTRIBUTES"},
         {"status.talents", "UI_STATUS_TALENTS"},
         {"status.protection", "UI_STATUS_PROTECTION"}}};
    const std::array<LayoutBox, 4> boxes = {
        LayoutBox{Anchor::TopLeft, Vec2(60.0f, 110.0f), Vec2(460.0f, 250.0f)},
        LayoutBox{Anchor::TopLeft, Vec2(60.0f, 390.0f), Vec2(460.0f, 280.0f)},
        LayoutBox{Anchor::TopRight, Vec2(-60.0f, 110.0f), Vec2(460.0f, 330.0f)},
        LayoutBox{Anchor::TopRight, Vec2(-60.0f, 470.0f), Vec2(460.0f, 200.0f)}};
    for (usize i = 0; i < columns.size(); ++i)
    {
        Widget& block = panel.add<Widget>(boxes[i]);
        block.layoutKey = columns[i].first;
        Column column;
        column.heading = &block.add<Label>("heading", UiText::key(columns[i].second));
        column.heading->box = {Anchor::Top, Vec2(0.0f), Vec2(0.0f, 36.0f)};
        column.labels = &block.add<Label>("body", UiText::literal(""));
        column.labels->box = {Anchor::Top, Vec2(0.0f, 44.0f), Vec2(0.0f, -44.0f)};
        column.values = &block.add<Label>("body", UiText::literal(""), Align::Right);
        column.values->box = {Anchor::Top, Vec2(0.0f, 44.0f), Vec2(0.0f, -44.0f)};
        m_columns.push_back(column);
    }
}

void StatusScreen::onUpdate(f32)
{
    if (m_source)
    {
        m_view = m_source();
    }
    m_title->text = UiText::literal(m_view.guild.empty() ? m_view.name : m_view.name + " – " + m_view.guild);
    const std::array<const std::vector<std::pair<std::string, std::string>>*, 4> rows = {
        &m_view.general, &m_view.attributes, &m_view.talents, &m_view.protection};
    for (usize i = 0; i < m_columns.size(); ++i)
    {
        m_columns[i].labels->text = UiText::literal(joined(*rows[i], false));
        m_columns[i].values->text = UiText::literal(joined(*rows[i], true));
    }
}

bool StatusScreen::onBack()
{
    if (m_onClose)
    {
        m_onClose();
    }
    close();
    return true;
}

// --- Diary ---

/// The diary's sections as tabs on the parchment.
class DiaryTabBar : public Widget
{
public:
    explicit DiaryTabBar(const DiaryScreen& screen) : m_screen(screen) {}
    [[nodiscard]] usize tabAt(Vec2 point) const noexcept
    {
        const f32 width = m_rect.width() / static_cast<f32>(kDiarySections.size());
        return std::min(kDiarySections.size() - 1,
                        static_cast<usize>(std::max(0.0f, point.x - m_rect.min.x) / width));
    }

protected:
    void drawSelf(const DrawContext& context) const override
    {
        const f32 width = m_rect.width() / static_cast<f32>(kDiarySections.size());
        for (usize i = 0; i < kDiarySections.size(); ++i)
        {
            const Rect rect{Vec2(m_rect.min.x + width * static_cast<f32>(i), m_rect.min.y),
                            Vec2(m_rect.min.x + width * static_cast<f32>(i + 1), m_rect.max.y)};
            const bool open = i == m_screen.m_section;
            drawFrame(context, rect, open ? "tab_active" : "tab");
            const TextLayout title =
                context.ui.layout("tab", context.ui.strings().get(kDiarySections[i]), context.scale(),
                                  {.wrapWidth = rect.width(), .maxLines = 1, .align = Align::Center});
            TextStyle style = context.ui.textStyle("tab", context.scale());
            if (open)
            {
                style.color = context.ui.style().color("highlight");
            }
            context.canvas.text(Vec2(rect.min.x, rect.min.y + (rect.height() - title.size.y) * 0.5f), title,
                                style);
        }
    }

private:
    const DiaryScreen& m_screen;
};

DiaryScreen::DiaryScreen(std::function<DiaryView()> view, std::function<void()> onClose)
    : m_source(std::move(view)), m_onClose(std::move(onClose))
{
    Panel& page = root().add<Panel>("parchment");
    page.layoutKey = "diary.page";
    page.box = {Anchor::Center, Vec2(0.0f), Vec2(1240.0f, 780.0f)};
    m_chapter = &page.add<Label>("document_title", UiText::literal(""), Align::Center);
    m_chapter->layoutKey = "diary.chapter";
    m_chapter->box = {Anchor::Top, Vec2(0.0f, 40.0f), Vec2(-120.0f, 48.0f)};
    DiaryTabBar& tabs = page.add<DiaryTabBar>(*this);
    tabs.layoutKey = "diary.tabs";
    tabs.box = {Anchor::Top, Vec2(0.0f, 100.0f), Vec2(-120.0f, 44.0f)};
    m_tabs = &tabs;
    m_topics = &page.add<List>("dialog", 38.0f);
    m_topics->layoutKey = "diary.topics";
    m_topics->box = {Anchor::TopLeft, Vec2(60.0f, 164.0f), Vec2(400.0f, -220.0f)};
    m_topics->highlightColor = "ink_highlight";
    m_entries = &page.add<TextView>("document");
    m_entries->layoutKey = "diary.entries";
    m_entries->box = {Anchor::TopRight, Vec2(-60.0f, 164.0f), Vec2(680.0f, -220.0f)};
    m_empty = &page.add<Label>("document", UiText::key("UI_DIARY_EMPTY"), Align::Center);
    m_empty->layoutKey = "diary.empty";
    m_empty->box = {Anchor::Center, Vec2(0.0f, 40.0f), Vec2(-120.0f, 40.0f)};
}

const std::vector<DiaryView::Topic>& DiaryScreen::current() const noexcept
{
    switch (m_section)
    {
    case 1:
        return m_view.done;
    case 2:
        return m_view.failed;
    case 3:
        return m_view.notes;
    default:
        return m_view.running;
    }
}

void DiaryScreen::onUpdate(f32)
{
    if (m_source)
    {
        m_view = m_source();
    }
    m_chapter->text = UiText::literal(m_view.chapter);
    const auto& topics = current();
    std::vector<std::string> names;
    for (const DiaryView::Topic& t : topics)
    {
        names.push_back(t.name);
    }
    if (names != m_shownTopics)
    {
        m_shownTopics = names;
        m_topics->rows.clear();
        for (const std::string& name : names)
        {
            m_topics->rows.push_back(UiText::literal(name));
        }
        m_topics->select(0);
    }
    m_empty->visible = topics.empty();
    m_topics->visible = !topics.empty();
    m_entries->visible = !topics.empty();
    const usize selected = m_topics->selected();
    std::string text;
    if (selected < topics.size())
    {
        for (const std::string& entry : topics[selected].entries)
        {
            text += (text.empty() ? "" : "\n\n") + entry;
        }
    }
    if (selected != m_shownTopic)
    {
        m_shownTopic = selected;
        m_entries->scrollToTop();
    }
    m_entries->text = std::move(text);
}

bool DiaryScreen::onInput(const UiInput& in)
{
    const Rect& bar = m_tabs->rect();
    if (in.click && in.pointer.x >= bar.min.x && in.pointer.x < bar.max.x && in.pointer.y >= bar.min.y &&
        in.pointer.y < bar.max.y)
    {
        const usize tab = static_cast<const DiaryTabBar*>(m_tabs)->tabAt(in.pointer);
        (void)onTab(static_cast<int>(tab) - static_cast<int>(m_section));
        emit("select");
        return true;
    }
    return false;
}

bool DiaryScreen::onTab(int delta)
{
    const auto count = static_cast<int>(kDiarySections.size());
    m_section = static_cast<usize>(((static_cast<int>(m_section) + delta) % count + count) % count);
    m_shownTopics.clear();
    m_shownTopic = static_cast<usize>(-1);
    return true;
}

bool DiaryScreen::onBack()
{
    if (m_onClose)
    {
        m_onClose();
    }
    close();
    return true;
}

// --- Map ---

/// The map picture, fitted into its box with its aspect ratio.
class MapPicture : public Widget
{
public:
    explicit MapPicture(const MapScreen& screen) : m_screen(screen) {}
    [[nodiscard]] Rect fitted() const noexcept
    {
        const Vec2 size = m_screen.m_imageSize;
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
        if (m_screen.m_image != nullptr)
        {
            context.canvas.image(fitted(), m_screen.m_image, {Vec2(0.0f), Vec2(1.0f)}, Color{});
        }
    }

private:
    const MapScreen& m_screen;
};

MapScreen::MapScreen(std::string title, const render::rhi::Texture* image, Vec2 imageSize,
                     std::function<void()> onClose)
    : m_onClose(std::move(onClose)), m_image(image), m_imageSize(imageSize)
{
    Panel& page = root().add<Panel>("parchment");
    page.layoutKey = "map.page";
    page.box = {Anchor::Center, Vec2(0.0f), Vec2(1400.0f, 940.0f)};
    Label& heading = page.add<Label>("document_title", UiText::literal(std::move(title)), Align::Center);
    heading.layoutKey = "map.title";
    heading.box = {Anchor::Top, Vec2(0.0f, 30.0f), Vec2(-120.0f, 46.0f)};
    MapPicture& picture = page.add<MapPicture>(*this);
    picture.layoutKey = "map.picture";
    picture.box = {Anchor::Top, Vec2(0.0f, 90.0f), Vec2(-100.0f, -130.0f)};
    m_page = &picture;
}

Rect MapScreen::imageRect() const noexcept
{
    return static_cast<const MapPicture*>(m_page)->fitted();
}

bool MapScreen::onBack()
{
    if (m_onClose)
    {
        m_onClose();
    }
    close();
    return true;
}
} // namespace g7::ui
