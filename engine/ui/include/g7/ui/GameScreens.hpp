#pragma once

// Game screens of M14 part C: the dialogue choices (replaces the ImGui DialogPanel; owner decision E3 from
// M10: like Gothic 1, up/down and the action key or the mouse), documents, choices at a mob (anvil, bed) and
// lockpicking. They read their data through a callback every frame and act through callbacks - the ui module
// knows no gameplay types (ADR 0009). All block the game controls.

#include <g7/ui/Screen.hpp>

#include <functional>
#include <string>
#include <vector>

namespace g7::ui
{
/// What the dialogue shows: while a line is spoken (subtitles in the HUD) nothing but accept = skip; then the
/// options.
struct DialogView
{
    bool menu = false;
    std::vector<std::string> options;
    usize selected = 0;
    bool hidden = false; ///< trading: the trade window has the screen (until part D)
};

class DialogScreen : public Screen
{
public:
    struct Actions
    {
        std::function<DialogView()> view;
        std::function<void()> skip;        ///< the spoken line (E4: lines can be skipped)
        std::function<void(usize)> choose; ///< an option
        std::function<void(usize)> select; ///< the selection moved (kept in the engine's dialogue)
    };
    explicit DialogScreen(Actions actions);
    [[nodiscard]] ScreenFlags flags() const override { return {.blocksGameInput = true, .hidesHud = true}; }
    [[nodiscard]] std::string_view name() const override { return "dialog"; }
    [[nodiscard]] List& options() noexcept { return *m_list; }

protected:
    void onUpdate(f32 deltaSeconds) override;
    bool onAccept() override;
    bool onBack() override { return false; } // a conversation is not left with Esc (Gothic 1)

private:
    Actions m_actions;
    DialogView m_view;
    std::vector<std::string> m_shown; ///< the options the list holds
    usize m_reported = 0;             ///< the selection last told to the engine
    Panel* m_panel = nullptr;
    List* m_list = nullptr;
};

/// A letter or book being read: title and text on parchment; accept or back closes it.
class DocumentScreen : public Screen
{
public:
    DocumentScreen(std::string title, std::string text, std::function<void()> onClose);
    [[nodiscard]] std::string_view name() const override { return "document"; }
    [[nodiscard]] const std::string& title() const noexcept { return m_title; }
    [[nodiscard]] const std::string& text() const noexcept { return m_text; }

protected:
    bool onAccept() override;
    bool onBack() override;

private:
    std::string m_title;
    std::string m_text;
    std::function<void()> m_onClose;
};

/// A choice at a mob: what to forge, how long to sleep; a message under the options (strikes left, results).
struct ChoiceView
{
    std::string title;
    std::vector<std::string> options;
    std::string message;
};

class ChoiceScreen : public Screen
{
public:
    struct Actions
    {
        std::function<ChoiceView()> view;
        std::function<void(usize)> choose;
        std::function<void()> leave;
    };
    explicit ChoiceScreen(Actions actions);
    [[nodiscard]] std::string_view name() const override { return "choice"; }
    [[nodiscard]] List& options() noexcept { return *m_list; }

protected:
    void onUpdate(f32 deltaSeconds) override;
    bool onBack() override;

private:
    Actions m_actions;
    std::vector<std::string> m_shown;
    Label* m_title = nullptr;
    Label* m_message = nullptr;
    List* m_list = nullptr;
};

/// Lockpicking: the combination's steps, the picks left, the last result; left/right turn, back stops.
struct LockpickView
{
    std::string title;
    usize progress = 0;
    usize length = 0;
    u32 picks = 0;
    std::string result;
};

class LockpickScreen : public Screen
{
public:
    struct Actions
    {
        std::function<LockpickView()> view;
        std::function<void(bool left)> turn;
        std::function<void()> leave;
    };
    explicit LockpickScreen(Actions actions);
    [[nodiscard]] std::string_view name() const override { return "lockpick"; }
    [[nodiscard]] const LockpickView& view() const noexcept { return m_view; }

protected:
    void onUpdate(f32 deltaSeconds) override;
    bool onDirection(NavDir dir) override;
    bool onBack() override;

private:
    Actions m_actions;
    LockpickView m_view;
    Label* m_title = nullptr;
    Label* m_picks = nullptr;
    Label* m_result = nullptr;
    Grid* m_steps = nullptr;
};
} // namespace g7::ui
