#pragma once

// The UI sample screen (M14 parts A and B, `gothar --ui-sample`): every text role, the frames, the HUD bars,
// and widgets to try the navigation with keyboard, gamepad and mouse - a list, a grid of cells, buttons.
// Boxes come from layout.toml ("sample.*", "hud.*"). ui_back or the close button closes it.

#include <g7/ui/Screen.hpp>

#include <memory>

namespace g7::ui
{
class SampleScreen : public Screen
{
public:
    SampleScreen();
    [[nodiscard]] std::string_view name() const override { return "sample"; }

    [[nodiscard]] List& list() noexcept { return *m_list; }
    [[nodiscard]] Grid& grid() noexcept { return *m_grid; }
    /// What the screen shows as chosen last (empty before anything was chosen).
    [[nodiscard]] const UiText& chosen() const noexcept;

private:
    List* m_list = nullptr;
    Grid* m_grid = nullptr;
    Label* m_chosen = nullptr;
};

[[nodiscard]] std::unique_ptr<Screen> makeSampleScreen();
} // namespace g7::ui
