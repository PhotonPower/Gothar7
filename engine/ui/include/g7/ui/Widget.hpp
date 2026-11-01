#pragma once

// Retained widgets of the game UI (M14 part B): a tree per screen, laid out from LayoutBoxes (fixed or named
// in layout.toml), drawn with the Canvas and the style. Mechanics only; texts come from keys or the screen's
// data.

#include <g7/core/Math.hpp>
#include <g7/core/Types.hpp>
#include <g7/ui/Canvas.hpp>
#include <g7/ui/Layout.hpp>
#include <g7/ui/Navigation.hpp>
#include <g7/ui/Strings.hpp>
#include <g7/ui/Text.hpp>

#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace g7::ui
{
class UiResources;
class Widget;

/// What drawing needs: the canvas, the resources and the focused widget.
struct DrawContext
{
    Canvas& canvas;
    const UiResources& ui;
    const Widget* focus = nullptr;
    [[nodiscard]] f32 scale() const noexcept { return canvas.uiScale(); }
};

/// A text shown by a widget: a key of the language's table (with optional {placeholders}) or a literal text
/// (names and values from the game). Keys are looked up when drawn, so changed tables show at once.
struct UiText
{
    std::string value;
    bool isKey = false;
    std::vector<std::pair<std::string, std::string>> args;

    [[nodiscard]] static UiText key(std::string name,
                                    std::vector<std::pair<std::string, std::string>> values = {})
    {
        return {std::move(name), true, std::move(values)};
    }
    [[nodiscard]] static UiText literal(std::string text) { return {std::move(text), false, {}}; }
    [[nodiscard]] std::string resolve(const Strings& strings) const;
};

class Widget
{
public:
    Widget() = default;
    explicit Widget(LayoutBox initial) : box(initial) {}
    virtual ~Widget() = default;
    Widget(const Widget&) = delete;
    Widget& operator=(const Widget&) = delete;

    /// Place in the parent (UI units). With `layoutKey` set, the box comes from layout.toml (falling back to
    /// `box`).
    LayoutBox box;
    std::string layoutKey;
    bool visible = true;
    /// Replaces the box height (UI units) after the layout lookup: content-sized widgets (dialogue choices).
    std::optional<f32> heightOverride;

    template <class T, class... Args>
    T& add(Args&&... args)
    {
        auto child = std::make_unique<T>(std::forward<Args>(args)...);
        T& ref = *child;
        m_children.push_back(std::move(child));
        return ref;
    }
    [[nodiscard]] std::span<const std::unique_ptr<Widget>> children() const noexcept { return m_children; }
    /// The rectangle in framebuffer pixels after the last arrange().
    [[nodiscard]] const Rect& rect() const noexcept { return m_rect; }

    /// Computes this widget's rectangle inside `parent` and then its children's.
    void arrange(const Rect& parent, f32 scale, const Layout* layout);
    /// Puts this widget at `rect` (pixels) as it is and arranges its children (containers place their
    /// children so).
    void arrangeAt(const Rect& rect, f32 scale, const Layout* layout);
    /// Draws itself, then its visible children.
    void draw(const DrawContext& context) const;
    /// Visible focusable widgets in tree order (parents before children).
    void collectFocusables(std::vector<Widget*>& out);
    /// The innermost visible focusable widget under `point`, or nullptr.
    [[nodiscard]] Widget* hit(Vec2 point);

    [[nodiscard]] virtual bool focusable() const { return false; }
    /// A step inside the widget (list row, grid cell); false: the widget's edge, focus moves on.
    virtual bool navigate(NavDir) { return false; }
    /// The accept action on the focused widget; true if it did something.
    virtual bool accept() { return false; }
    /// Mouse wheel steps over the widget; true if it scrolled.
    virtual bool scroll(f32) { return false; }
    /// The mouse over the widget (selects the row or cell under it); true if the selection changed.
    virtual bool pointerAt(Vec2) { return false; }

protected:
    /// Places the children; the default lays each out in this widget's rectangle.
    virtual void arrangeChildren(f32 scale, const Layout* layout);
    virtual void drawSelf(const DrawContext&) const {}

    Rect m_rect;
    std::vector<std::unique_ptr<Widget>> m_children;
};

/// A 9-slice frame from the style ("panel", "parchment" ...).
class Panel : public Widget
{
public:
    explicit Panel(std::string frameName = "panel") : frame(std::move(frameName)) {}
    std::string frame;

protected:
    void drawSelf(const DrawContext& context) const override;
};

/// A text in a style role, wrapped to the widget's width.
class Label : public Widget
{
public:
    Label(std::string textRole, UiText content, Align alignment = Align::Left)
        : role(std::move(textRole)), text(std::move(content)), align(alignment)
    {
    }
    std::string role;
    UiText text;
    Align align = Align::Left;
    u32 maxLines = 0;
    f32 opacity = 1.0f; ///< fades text, outline and shadow

protected:
    void drawSelf(const DrawContext& context) const override;
};

/// A filled bar (health, mana, enemy, air): dark back, the fill by `fraction`, the rim frame over it.
class Bar : public Widget
{
public:
    Bar(std::string fillFrame, std::string fillColor, f32 filled = 1.0f)
        : fill(std::move(fillFrame)), color(std::move(fillColor)), fraction(filled)
    {
    }
    std::string fill;  ///< frame of the fill ("bar_hp"), already coloured; without it the colour below
    std::string color; ///< style colour of the fill without a fill frame
    f32 fraction = 1.0f;
    std::string back = "bar_back"; ///< style colour behind the fill
    std::string frame = "bar";     ///< drawn over the fill; empty: none
    Vec2 inset{7.0f, 5.0f};        ///< UI units between the bar's edge and the fill

protected:
    void drawSelf(const DrawContext& context) const override;
};

/// A focusable text that does something on accept (or click).
class Button : public Widget
{
public:
    Button(UiText label, std::function<void()> action, std::string textRole = "body")
        : text(std::move(label)), onAccept(std::move(action)), role(std::move(textRole))
    {
    }
    UiText text;
    std::function<void()> onAccept;
    std::string role;

    [[nodiscard]] bool focusable() const override { return true; }
    bool accept() override;

protected:
    void drawSelf(const DrawContext& context) const override;
};

/// Children one after another, vertically or horizontally. A child's size along the stack comes from its box
/// (UI units); children with size <= 0 there share the rest equally. Across the stack the child's box
/// applies.
class Stack : public Widget
{
public:
    enum class Direction : u8
    {
        Vertical,
        Horizontal,
    };
    explicit Stack(Direction stackDirection = Direction::Vertical, f32 gap = 0.0f)
        : direction(stackDirection), spacing(gap)
    {
    }
    Direction direction;
    f32 spacing; ///< UI units between children

protected:
    void arrangeChildren(f32 scale, const Layout* layout) override;
};

/// A scrolling list of text rows with a selection (dialogue options, diary entries, menus).
class List : public Widget
{
public:
    explicit List(std::string textRole = "body", f32 rowUnits = 36.0f)
        : role(std::move(textRole)), rowHeight(rowUnits)
    {
    }
    std::string role;
    f32 rowHeight; ///< UI units
    std::vector<UiText> rows;
    /// Optional second column, right-aligned in the same row (option values, M14 part F).
    std::vector<UiText> values;
    /// Rows with a slider in the value column instead of a text: 0..1 (option volumes, frames slider_track
    /// and slider_knob).
    std::vector<std::optional<f32>> sliders;
    /// Rows shown greyed out (`text_dim`) that cannot be accepted (Load and Save until M15).
    std::vector<bool> dimmed;
    std::string highlightColor = "highlight"; ///< style colour of the selected row's text while focused
    std::function<void(usize)> onAccept;

    [[nodiscard]] bool isDimmed(usize row) const noexcept { return row < dimmed.size() && dimmed[row]; }

    [[nodiscard]] usize selected() const noexcept { return m_selected; }
    void select(usize row);
    [[nodiscard]] usize firstVisible() const noexcept { return m_first; }
    [[nodiscard]] usize visibleRows() const noexcept { return m_visible; }

    [[nodiscard]] bool focusable() const override { return !rows.empty(); }
    bool navigate(NavDir dir) override;
    bool accept() override;
    bool scroll(f32 steps) override;
    bool pointerAt(Vec2 point) override;

protected:
    void arrangeChildren(f32 scale, const Layout* layout) override;
    void drawSelf(const DrawContext& context) const override;

private:
    void keepVisible();
    usize m_selected = 0;
    usize m_first = 0;
    usize m_visible = 1;
    f32 m_rowPixels = 1.0f;
};

/// A grid of square cells with a selection (inventory). Cells are drawn by `drawCell` (frame "cell" or
/// "cell_selected" without one).
class Grid : public Widget
{
public:
    Grid(usize cells = 0, usize columnCount = 5, f32 cellUnits = 104.0f, f32 gap = 6.0f)
        : count(cells), columns(columnCount), cell(cellUnits), spacing(gap)
    {
    }
    usize count;
    usize columns;
    f32 cell;    ///< UI units
    f32 spacing; ///< UI units
    std::function<void(const DrawContext&, const Rect&, usize index, bool selected)> drawCell;
    std::function<void(usize)> onAccept;

    [[nodiscard]] usize selected() const noexcept { return m_selected; }
    void select(usize index);
    [[nodiscard]] usize firstRow() const noexcept { return m_firstRow; }
    /// The cell's rectangle in pixels (also for cells scrolled out of view).
    [[nodiscard]] Rect cellRect(usize index) const noexcept;

    bool interactive = true; ///< false: display only (no focus, no selection)
    [[nodiscard]] bool focusable() const override { return interactive && count > 0; }
    bool navigate(NavDir dir) override;
    bool accept() override;
    bool scroll(f32 steps) override;
    bool pointerAt(Vec2 point) override;

protected:
    void arrangeChildren(f32 scale, const Layout* layout) override;
    void drawSelf(const DrawContext& context) const override;

private:
    void keepVisible();
    [[nodiscard]] usize rows() const noexcept;
    usize m_selected = 0;
    usize m_firstRow = 0;
    usize m_visibleRows = 1;
    f32 m_cellPixels = 1.0f;
    f32 m_spacingPixels = 0.0f;
};
} // namespace g7::ui
