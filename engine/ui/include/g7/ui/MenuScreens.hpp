#pragma once

// Main menu, pause menu, options and loading screen (M14 part F). The screens show and change data through
// callbacks; what an option means (a config key, a window mode, a key binding) the engine decides.

#include <g7/core/Types.hpp>
#include <g7/ui/Screen.hpp>

#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace g7::platform
{
class ActionMap;
enum class Action : u16;
enum class Key : u16;
} // namespace g7::platform

namespace g7::ui
{
// --- Main and pause menu ---

struct MenuEntry
{
    std::string id;      ///< handed to Actions::choose ("new_game", "options" ...)
    std::string textKey; ///< UI text key
    bool enabled = true; ///< disabled entries are greyed out (Load and Save until M15)
};

/// A list of entries over the running scene: the main menu (title above, F8) or the pause menu (Esc in the
/// game). It pauses the game and hides the HUD.
class MenuScreen : public Screen
{
public:
    struct Actions
    {
        std::function<void(std::string_view id)> choose;
        /// Going back: the pause menu resumes; unset (main menu): nothing happens.
        std::function<void()> back;
    };
    MenuScreen(std::vector<MenuEntry> entries, Actions actions, bool title);
    [[nodiscard]] std::string_view name() const override { return "menu"; }
    [[nodiscard]] ScreenFlags flags() const override { return {true, true, true}; }
    [[nodiscard]] List& entries() noexcept { return *m_list; }
    /// Selects the entry `id` (if there is one).
    void select(std::string_view id);

protected:
    bool onBack() override;

private:
    std::vector<MenuEntry> m_entries;
    Actions m_actions;
    List* m_list = nullptr;
};

// --- Options ---

using OptionValue = std::variant<bool, i64, f64, std::string>;

enum class OptionKind : u8
{
    Slider,  ///< f64 from min to max in steps, shown in percent
    Toggle,  ///< bool, on/off
    Choice,  ///< one of `choices`
    Binding, ///< the keys of an action; accept waits for a key
};

struct OptionChoice
{
    OptionValue value;
    UiText label;
};

struct OptionDef
{
    std::string id;    ///< config key ("audio.music") or, for a binding, the action name ("jump")
    std::string label; ///< UI text key
    OptionKind kind = OptionKind::Slider;
    f64 min = 0.0;
    f64 max = 1.0;
    f64 step = 0.05;
    std::vector<OptionChoice> choices;
};

struct OptionPage
{
    std::string title; ///< UI text key of the tab
    std::vector<OptionDef> options;
};

/// One step left (-1) or right (+1): sliders clamp, toggles flip, choices cycle. Bindings stay.
[[nodiscard]] OptionValue stepOption(const OptionDef& option, const OptionValue& current, int direction);
/// The value as shown: "80 %", on/off, the choice's label (the value itself if it is none of them).
[[nodiscard]] UiText optionText(const OptionDef& option, const OptionValue& value);

/// Assigns `key` to `action` as its first key: the action's previous first key goes, a second keyboard key,
/// mouse buttons and gamepad buttons stay. Actions in `group` (the assignable ones) that had the key lose it;
/// they are returned so the screen can say so. Actions outside the group (menus, debug keys) are untouched.
std::vector<platform::Action> assignKey(platform::ActionMap& map, platform::Action action, platform::Key key,
                                        std::span<const platform::Action> group);

/// Tabs of options; up/down choose, left/right (or accept) change, a binding waits for a key after accept
/// (Esc cancels), the tab keys switch pages, back leaves (Actions::close saves).
class OptionsScreen : public Screen
{
public:
    struct Actions
    {
        std::function<OptionValue(const OptionDef&)> get;
        std::function<void(const OptionDef&, const OptionValue&)> set;
        /// A binding's keys as text ("Alt, Pad A").
        std::function<std::string(const OptionDef&)> binding;
        /// Assigns the key to the binding; returns a message for the player (a conflict), may be empty.
        std::function<std::string(const OptionDef&, platform::Key)> assign;
        std::function<void()> close;
    };
    OptionsScreen(std::vector<OptionPage> pages, Actions actions);
    [[nodiscard]] std::string_view name() const override { return "options"; }
    [[nodiscard]] ScreenFlags flags() const override { return {true, true, true}; }
    [[nodiscard]] usize page() const noexcept { return m_page; }
    /// Shows the tab `page` (0-based, clamped).
    void selectPage(usize page) { showPage(page); }
    [[nodiscard]] List& options() noexcept { return *m_list; }
    /// The binding waiting for a key, if any.
    [[nodiscard]] const OptionDef* waiting() const noexcept;
    [[nodiscard]] const std::string& message() const noexcept { return m_message; }

protected:
    void onUpdate(f32 deltaSeconds) override;
    bool onInput(const UiInput& input) override;
    bool onDirection(NavDir dir) override;
    bool onTab(int direction) override;
    bool onBack() override;

private:
    void showPage(usize page);
    void change(usize row, int direction);
    [[nodiscard]] const OptionDef* option(usize row) const;

    std::vector<OptionPage> m_pages;
    Actions m_actions;
    usize m_page = 0;
    std::optional<usize> m_waiting; ///< row of the binding waiting for a key
    std::string m_message;
    std::vector<Label*> m_tabs;
    std::vector<Panel*> m_tabFrames;
    List* m_list = nullptr;
    Label* m_hint = nullptr;
};

// --- Loading screen ---

struct LoadingView
{
    std::string world;   ///< shown name
    f32 progress = 0.0f; ///< 0..1
};

/// Covers the screen while a world loads: background, world name, progress. Cannot be closed by the player.
class LoadingScreen : public Screen
{
public:
    explicit LoadingScreen(std::function<LoadingView()> view);
    [[nodiscard]] std::string_view name() const override { return "loading"; }
    [[nodiscard]] ScreenFlags flags() const override { return {true, true, true}; }

protected:
    void onUpdate(f32 deltaSeconds) override;
    bool onBack() override { return true; }

private:
    std::function<LoadingView()> m_view;
    Label* m_title = nullptr;
    Bar* m_bar = nullptr;
};
} // namespace g7::ui
