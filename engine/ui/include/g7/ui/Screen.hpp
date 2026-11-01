#pragma once

// Screens of the game UI and their stack (M14 part B). A screen owns a widget tree, keeps the focus and turns
// UiInput into focus moves, accepts and going back. The stack decides who gets the input and whether the game
// does: a blocking screen takes the game controls away, a pausing one stops the simulation.

#include <g7/core/Types.hpp>
#include <g7/ui/Navigation.hpp>
#include <g7/ui/Widget.hpp>

#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace g7::ui
{
class Canvas;
class Layout;
class UiResources;

struct ScreenFlags
{
    bool blocksGameInput = true; ///< the game controls are off while the screen is open
    bool pausesGame = false;     ///< the simulation stops (main menu, F8)
    bool hidesHud = false;
};

class Screen
{
public:
    Screen() = default;
    virtual ~Screen() = default;
    Screen(const Screen&) = delete;
    Screen& operator=(const Screen&) = delete;

    [[nodiscard]] virtual ScreenFlags flags() const { return {}; }
    /// The name for logs and tests.
    [[nodiscard]] virtual std::string_view name() const { return "screen"; }

    [[nodiscard]] Widget& root() noexcept { return m_root; }
    [[nodiscard]] const Widget& root() const noexcept { return m_root; }
    [[nodiscard]] Widget* focus() const noexcept { return m_focus; }
    void setFocus(Widget* widget) noexcept { m_focus = widget; }

    /// Lays the tree out in `area` and, with `input`, moves the focus and runs accepts and going back.
    void update(const UiInput* input, f32 deltaSeconds, const Rect& area, f32 scale, const Layout* layout);
    void draw(Canvas& canvas, const UiResources& ui) const;

    void close() noexcept { m_closed = true; }
    [[nodiscard]] bool closed() const noexcept { return m_closed; }
    /// UI events since the last call ("select", "accept", "back"): the engine plays their sounds.
    [[nodiscard]] std::vector<std::string> takeEvents();

protected:
    /// Going back (ui_back); the default closes the screen. True if it did something.
    virtual bool onBack()
    {
        close();
        return true;
    }
    /// ui_prev_tab (-1) or ui_next_tab (+1); true if it did something.
    virtual bool onTab(int) { return false; }
    /// Once per update before the input: refresh widgets from the data shown.
    virtual void onUpdate(f32) {}
    void emit(std::string_view event) { m_events.emplace_back(event); }

private:
    Widget m_root;
    Widget* m_focus = nullptr;
    bool m_closed = false;
    std::vector<std::string> m_events;
};

class ScreenStack
{
public:
    void push(std::unique_ptr<Screen> screen);
    /// Removes the top screen.
    void pop();
    void clear();
    [[nodiscard]] Screen* top() const noexcept
    {
        return m_screens.empty() ? nullptr : m_screens.back().get();
    }
    [[nodiscard]] usize size() const noexcept { return m_screens.size(); }
    [[nodiscard]] bool empty() const noexcept { return m_screens.empty(); }
    [[nodiscard]] Screen* find(std::string_view name) const noexcept;

    [[nodiscard]] bool blocksGameInput() const noexcept;
    [[nodiscard]] bool pausesGame() const noexcept;
    [[nodiscard]] bool hidesHud() const noexcept;

    /// The top screen gets the input, the others are only laid out; closed screens are removed afterwards.
    void update(const UiInput& input, f32 deltaSeconds, const Rect& area, f32 scale, const Layout* layout);
    /// Bottom to top.
    void draw(Canvas& canvas, const UiResources& ui) const;
    /// Events of all screens, including those closed in the last update.
    [[nodiscard]] std::vector<std::string> takeEvents();

private:
    std::vector<std::unique_ptr<Screen>> m_screens;
    std::vector<std::string> m_events;
};
} // namespace g7::ui
