#pragma once

// Saving and loading (M15 part D, docs/modules/save.md): the save slots in a list with the selected one's
// thumbnail; choosing a slot may ask first (overwrite, unsaved progress). The screen shows what the engine
// says and reports the choice; reading and writing files is the engine's.

#include <g7/core/Math.hpp>
#include <g7/ui/Screen.hpp>

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace g7::render::rhi
{
class Texture;
}

namespace g7::ui
{
struct SaveSlotRow
{
    std::string slot;   ///< "slot_03", "quick", "auto"
    std::string title;  ///< the save's name, or "– leer –" / why it cannot be read
    std::string detail; ///< day and time of the game, date saved
    bool usable = true; ///< Load: an empty or broken slot cannot be chosen (greyed out)
};

struct SaveScreenView
{
    std::vector<SaveSlotRow> rows;
    const render::rhi::Texture* thumbnail = nullptr; ///< of the selected row, nullptr: none
    Vec2 thumbnailSize{320.0f, 180.0f};
    std::string message; ///< a line under the list (what went wrong)
};

/// The list of save slots for saving or loading. Up/down choose, accept takes the slot - after a question if
/// Actions::question has one (accept again confirms, back cancels) -, back leaves.
class SaveScreen : public Screen
{
public:
    enum class Mode : u8
    {
        Save,
        Load,
    };
    struct Actions
    {
        std::function<SaveScreenView()> view;
        std::function<void(usize row)> select; ///< the selected row changed (the thumbnail follows)
        /// A question before taking the row ("Spielstand überschreiben?"); empty: none.
        std::function<std::string(usize row)> question;
        std::function<void(usize row)> choose;
        std::function<void()> close;
    };
    SaveScreen(Mode mode, Actions actions);
    [[nodiscard]] std::string_view name() const override { return m_mode == Mode::Save ? "save" : "load"; }
    [[nodiscard]] ScreenFlags flags() const override { return {true, true, true}; }
    [[nodiscard]] Mode mode() const noexcept { return m_mode; }
    [[nodiscard]] List& slots() noexcept { return *m_list; }
    [[nodiscard]] const SaveScreenView& view() const noexcept { return m_view; }
    /// The question waiting for an answer, empty: none.
    [[nodiscard]] const std::string& asking() const noexcept { return m_asking; }
    [[nodiscard]] Rect thumbnailRect() const noexcept;

protected:
    void onUpdate(f32 deltaSeconds) override;
    bool onBack() override;

private:
    void take(usize row);

    Mode m_mode;
    Actions m_actions;
    SaveScreenView m_view;
    std::optional<usize> m_reported;
    std::optional<usize> m_askingRow;
    std::string m_asking;
    List* m_list = nullptr;
    Label* m_message = nullptr;
    Widget* m_picture = nullptr;

    friend class SaveThumbnail;
};
} // namespace g7::ui
