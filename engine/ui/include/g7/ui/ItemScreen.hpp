#pragma once

// The item screens of M14 part D (owner decisions F4, F5): the inventory as in Gothic 1 - category tabs, a
// grid of cells with the item's 3D model, the selected item large in the info box, the world goes on - and
// with a second pane trading (quantity choice for stacks) and containers (chest, looting). Data and actions
// come from the engine through callbacks; the ui module knows no gameplay types (ADR 0009).

#include <g7/render/rhi/Resources.hpp>
#include <g7/ui/Screen.hpp>

#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace g7::ui
{
class Layout;

enum class ItemScreenMode : u8
{
    Inventory, ///< the hero's items alone
    Trade,     ///< the trader's (left) and the hero's (right), prices in the info box
    Container, ///< a chest or a lying body (left) and the hero's (right)
};

struct ItemEntry
{
    std::string item; ///< instance name
    std::string name; ///< shown
    std::string category;
    u32 count = 1;
    bool equipped = false;
    std::vector<std::string> details; ///< lines of the info box (damage, protection, value ...)
    i64 price = 0;                    ///< trading: per piece
};

struct ItemPane
{
    std::string title;
    std::vector<ItemEntry> items;
    std::string footer; ///< "123 Gulden"
};

struct ItemScreenView
{
    ItemPane hero;
    ItemPane other; ///< trader or container (not in the inventory)
    std::string message;
};

/// A rendered item picture: a tile of an atlas of display values.
struct PreviewImage
{
    const render::rhi::Texture* texture = nullptr;
    Rect uv;
};

/// A tab: its title key and the item categories it shows.
struct ItemTab
{
    std::string key;
    std::vector<std::string> categories;
};

/// The tabs of layout.toml `[inventory] tabs = [{ key, categories }, ...]`; Gothic-like defaults without
/// them.
[[nodiscard]] std::vector<ItemTab> itemTabs(const Layout& layout);

class ItemScreen : public Screen
{
public:
    struct Actions
    {
        std::function<ItemScreenView()> view;
        std::function<void(const ItemEntry&)> use;  ///< inventory: use, equip or unequip
        std::function<void(const ItemEntry&)> drop; ///< inventory: drop one
        std::function<void(bool fromOther, const ItemEntry&, u32)> transfer; ///< buy/sell, take/put
        std::function<u32(bool fromOther, const ItemEntry&)> maxTransfer;    ///< money limits; default: count
        std::function<void()> close;
        /// The item's picture (large: the info box; spinning: the selected item turns).
        std::function<std::optional<PreviewImage>(const std::string& item, bool large, bool spinning)>
            preview;
    };

    ItemScreen(ItemScreenMode mode, Actions actions, std::vector<ItemTab> tabs);
    [[nodiscard]] ScreenFlags flags() const override
    {
        return {.blocksGameInput = true, .pausesGame = false};
    }
    [[nodiscard]] std::string_view name() const override;
    [[nodiscard]] ItemScreenMode mode() const noexcept { return m_mode; }

    // Inspection (tests, engine).
    [[nodiscard]] usize tab() const noexcept { return m_tab; }
    [[nodiscard]] const std::vector<ItemTab>& tabs() const noexcept { return m_tabs; }
    [[nodiscard]] Grid& heroGrid() noexcept { return *m_heroGrid; }
    [[nodiscard]] Grid& otherGrid() noexcept { return *m_otherGrid; }
    /// The entry under the focused grid's selection (the info box shows it), or nullptr.
    [[nodiscard]] const ItemEntry* selected() const noexcept;
    /// Whether that entry is in the other pane.
    [[nodiscard]] bool selectedInOther() const noexcept;
    [[nodiscard]] bool quantityOpen() const noexcept { return m_quantity.has_value(); }
    [[nodiscard]] u32 quantity() const noexcept { return m_quantity ? m_quantity->amount : 0; }
    [[nodiscard]] const std::vector<ItemEntry>& heroItems() const noexcept { return m_heroItems; }
    [[nodiscard]] const std::vector<ItemEntry>& otherItems() const noexcept { return m_otherItems; }

protected:
    void onUpdate(f32 deltaSeconds) override;
    bool onInput(const UiInput& input) override;
    bool onTab(int delta) override;
    bool onBack() override;

private:
    struct Quantity
    {
        bool fromOther = false;
        ItemEntry entry;
        u32 amount = 1;
        u32 max = 1;
    };
    void filter();
    void startTransfer(bool fromOther, const ItemEntry& entry);
    void drawCell(const DrawContext& context, const Rect& rect, const ItemEntry& entry, bool selected) const;

    ItemScreenMode m_mode;
    Actions m_actions;
    std::vector<ItemTab> m_tabs;
    usize m_tab = 0;
    bool m_tabChosen = false;
    ItemScreenView m_view;
    std::vector<ItemEntry> m_heroItems;  ///< filtered by the tab
    std::vector<ItemEntry> m_otherItems; ///< filtered by the tab
    std::optional<Quantity> m_quantity;
    Panel* m_otherPanel = nullptr;
    Label* m_heroTitle = nullptr;
    Label* m_otherTitle = nullptr;
    Label* m_heroFooter = nullptr;
    Label* m_otherFooter = nullptr;
    Label* m_message = nullptr;
    Widget* m_tabBar = nullptr;
    Grid* m_heroGrid = nullptr;
    Grid* m_otherGrid = nullptr;
    Widget* m_info = nullptr;
    Widget* m_quantityPanel = nullptr;

    friend class ItemTabBar;
    friend class ItemInfoBox;
    friend class ItemQuantityBox;
};
} // namespace g7::ui
