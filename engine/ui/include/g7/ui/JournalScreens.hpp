#pragma once

// Character, diary and map screens of M14 part E. Like the other game screens they read their data through a
// callback and know no gameplay types (ADR 0009).

#include <g7/render/rhi/Resources.hpp>
#include <g7/ui/Screen.hpp>

#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace g7::ui
{
/// A scrolling block of wrapped text (diary entries, long documents): focusable while the text is longer than
/// the box; up/down and the wheel scroll by lines.
class TextView : public Widget
{
public:
    explicit TextView(std::string textRole = "body") : role(std::move(textRole)) {}
    std::string role;
    std::string text;
    [[nodiscard]] f32 scrollOffset() const noexcept { return m_offset; }
    [[nodiscard]] bool scrollable() const noexcept { return m_contentHeight > m_rect.height() + 0.5f; }
    void scrollToTop() noexcept { m_offset = 0.0f; }

    [[nodiscard]] bool focusable() const override { return scrollable(); }
    bool navigate(NavDir dir) override;
    bool scroll(f32 steps) override;

protected:
    void drawSelf(const DrawContext& context) const override;

private:
    bool scrollBy(f32 lines);
    f32 m_offset = 0.0f; ///< pixels
    mutable f32 m_contentHeight = 0.0f;
    mutable f32 m_lineHeight = 20.0f;
};

/// The character sheet: name, guild, level and experience, learning points, attributes, talents, protection.
struct StatusView
{
    std::string name;
    std::string guild;
    std::vector<std::pair<std::string, std::string>> general;    ///< label, value (level, experience ...)
    std::vector<std::pair<std::string, std::string>> attributes; ///< label, value
    std::vector<std::pair<std::string, std::string>> talents;    ///< label, level
    std::vector<std::pair<std::string, std::string>> protection; ///< label, value
};

class StatusScreen : public Screen
{
public:
    StatusScreen(std::function<StatusView()> view, std::function<void()> onClose);
    [[nodiscard]] std::string_view name() const override { return "status"; }
    [[nodiscard]] const StatusView& view() const noexcept { return m_view; }

protected:
    void onUpdate(f32 deltaSeconds) override;
    bool onBack() override;

private:
    struct Column
    {
        Label* heading = nullptr;
        Label* labels = nullptr;
        Label* values = nullptr;
    };
    std::function<StatusView()> m_source;
    std::function<void()> m_onClose;
    StatusView m_view;
    Label* m_title = nullptr;
    std::vector<Column> m_columns;
};

/// The diary: chapter, quests by status with their entries, general notes by topic.
struct DiaryView
{
    std::string chapter;
    struct Topic
    {
        std::string name;
        std::vector<std::string> entries;
    };
    std::vector<Topic> running;
    std::vector<Topic> done;
    std::vector<Topic> failed;
    std::vector<Topic> notes;
};

class DiaryScreen : public Screen
{
public:
    DiaryScreen(std::function<DiaryView()> view, std::function<void()> onClose);
    [[nodiscard]] std::string_view name() const override { return "diary"; }
    /// 0 running, 1 done, 2 failed, 3 notes.
    [[nodiscard]] usize section() const noexcept { return m_section; }
    [[nodiscard]] List& topics() noexcept { return *m_topics; }
    [[nodiscard]] TextView& entries() noexcept { return *m_entries; }

protected:
    void onUpdate(f32 deltaSeconds) override;
    bool onInput(const UiInput& input) override;
    bool onTab(int delta) override;
    bool onBack() override;

private:
    [[nodiscard]] const std::vector<DiaryView::Topic>& current() const noexcept;
    std::function<DiaryView()> m_source;
    std::function<void()> m_onClose;
    DiaryView m_view;
    usize m_section = 0;
    std::vector<std::string> m_shownTopics;
    usize m_shownTopic = static_cast<usize>(-1);
    Label* m_chapter = nullptr;
    Widget* m_tabs = nullptr;
    List* m_topics = nullptr;
    TextView* m_entries = nullptr;
    Label* m_empty = nullptr;

    friend class DiaryTabBar;
};

/// A map (owner decision F9): the map item's picture on parchment, fitted, without the player's position.
class MapScreen : public Screen
{
public:
    MapScreen(std::string title, const render::rhi::Texture* image, Vec2 imageSize,
              std::function<void()> onClose);
    [[nodiscard]] std::string_view name() const override { return "map"; }
    [[nodiscard]] const render::rhi::Texture* image() const noexcept { return m_image; }
    /// Where the picture is drawn (aspect kept, inside the page).
    [[nodiscard]] Rect imageRect() const noexcept;

protected:
    bool onBack() override;
    bool onAccept() override { return onBack(); }

private:
    std::function<void()> m_onClose;
    const render::rhi::Texture* m_image = nullptr;
    Vec2 m_imageSize{1.0f};
    Widget* m_page = nullptr;

    friend class MapPicture;
};
} // namespace g7::ui
