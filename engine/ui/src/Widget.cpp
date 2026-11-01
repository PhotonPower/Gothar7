#include <g7/ui/UiResources.hpp>
#include <g7/ui/Widget.hpp>

#include <algorithm>
#include <cmath>

namespace g7::ui
{
namespace
{
/// The text style of a role; `highlight` swaps the colour for a style colour (focused entries).
TextStyle roleStyle(const DrawContext& context, std::string_view role, bool highlight,
                    std::string_view highlightColor = "highlight")
{
    TextStyle style = context.ui.textStyle(role, context.scale());
    if (highlight)
    {
        style.color = context.ui.style().color(highlightColor);
    }
    return style;
}

void drawFrame(const DrawContext& context, const Rect& rect, std::string_view frame)
{
    if (!frame.empty())
    {
        context.canvas.frame(rect, context.ui.frame(frame, context.scale()), context.ui.frameTint(frame));
    }
}

/// A one-line text vertically centred in `rect`.
void drawCentredLine(const DrawContext& context, const Rect& rect, std::string_view role,
                     std::string_view text, Align align, bool highlight, f32 inset,
                     std::string_view highlightColor = "highlight")
{
    const f32 s = context.scale();
    const TextLayout layout = context.ui.layout(
        role, text, s,
        {.wrapWidth = std::max(1.0f, rect.width() - 2.0f * inset * s), .maxLines = 1, .align = align});
    const Vec2 at(rect.min.x + inset * s, rect.min.y + (rect.height() - layout.size.y) * 0.5f);
    context.canvas.text(at, layout, roleStyle(context, role, highlight, highlightColor));
}
} // namespace

std::string UiText::resolve(const Strings& strings) const
{
    std::string text = isKey ? std::string(strings.get(value)) : value;
    for (const auto& [name, replacement] : args)
    {
        const std::string placeholder = "{" + name + "}";
        for (usize at = text.find(placeholder); at != std::string::npos;
             at = text.find(placeholder, at + replacement.size()))
        {
            text.replace(at, placeholder.size(), replacement);
        }
    }
    return text;
}

// --- Widget ---

void Widget::arrange(const Rect& parent, f32 scale, const Layout* layout)
{
    LayoutBox effective = layout != nullptr && !layoutKey.empty() ? layout->box(layoutKey, box) : box;
    if (heightOverride)
    {
        effective.size.y = *heightOverride;
    }
    arrangeAt(place(effective, parent, scale), scale, layout);
}

void Widget::arrangeAt(const Rect& rect, f32 scale, const Layout* layout)
{
    m_rect = rect;
    arrangeChildren(scale, layout);
}

void Widget::arrangeChildren(f32 scale, const Layout* layout)
{
    for (const auto& child : m_children)
    {
        child->arrange(m_rect, scale, layout);
    }
}

void Widget::draw(const DrawContext& context) const
{
    if (!visible)
    {
        return;
    }
    drawSelf(context);
    for (const auto& child : m_children)
    {
        child->draw(context);
    }
}

void Widget::collectFocusables(std::vector<Widget*>& out)
{
    if (!visible)
    {
        return;
    }
    if (focusable())
    {
        out.push_back(this);
    }
    for (const auto& child : m_children)
    {
        child->collectFocusables(out);
    }
}

Widget* Widget::hit(Vec2 point)
{
    if (!visible)
    {
        return nullptr;
    }
    for (auto it = m_children.rbegin(); it != m_children.rend(); ++it) // last drawn is on top
    {
        if (Widget* found = (*it)->hit(point))
        {
            return found;
        }
    }
    const bool inside = point.x >= m_rect.min.x && point.x < m_rect.max.x && point.y >= m_rect.min.y &&
                        point.y < m_rect.max.y;
    return inside && focusable() ? this : nullptr;
}

// --- Panel, Label, Bar, Button ---

void Panel::drawSelf(const DrawContext& context) const
{
    drawFrame(context, m_rect, frame);
}

void Label::drawSelf(const DrawContext& context) const
{
    const f32 s = context.scale();
    const TextLayout layout = context.ui.layout(
        role, text.resolve(context.ui.strings()), s,
        {.wrapWidth = std::max(1.0f, m_rect.width()), .maxLines = maxLines, .align = align});
    context.canvas.text(m_rect.min, layout, roleStyle(context, role, false).faded(opacity));
}

void Bar::drawSelf(const DrawContext& context) const
{
    const Style& style = context.ui.style();
    const Vec2 pad = inset * context.scale();
    Rect area{m_rect.min + pad, m_rect.max - pad};
    context.canvas.rect(area, style.color(back));
    area.max.x = area.min.x + std::max(0.0f, area.width()) * std::clamp(fraction, 0.0f, 1.0f);
    if (!fill.empty() && style.frames.contains(fill))
    {
        drawFrame(context, area, fill);
    }
    else
    {
        context.canvas.rect(area, style.color(color));
    }
    drawFrame(context, m_rect, frame);
}

bool Button::accept()
{
    if (onAccept)
    {
        onAccept();
        return true;
    }
    return false;
}

void Button::drawSelf(const DrawContext& context) const
{
    const bool focused = context.focus == this;
    drawFrame(context, m_rect, focused ? "button_focus" : "button");
    drawCentredLine(context, m_rect, role, text.resolve(context.ui.strings()), Align::Center, focused, 12.0f);
}

// --- Stack ---

void Stack::arrangeChildren(f32 scale, const Layout* layout)
{
    const int along = direction == Direction::Vertical ? 1 : 0;
    const int across = 1 - along;
    const f32 total = m_rect.size()[along];
    f32 fixed = 0.0f;
    usize flexible = 0;
    usize shown = 0;
    std::vector<LayoutBox> boxes;
    boxes.reserve(m_children.size());
    for (const auto& child : m_children)
    {
        const LayoutBox b = layout != nullptr && !child->layoutKey.empty()
                                ? layout->box(child->layoutKey, child->box)
                                : child->box;
        boxes.push_back(b);
        if (!child->visible)
        {
            continue;
        }
        ++shown;
        if (b.size[along] > 0.0f)
        {
            fixed += b.size[along] * scale;
        }
        else
        {
            ++flexible;
        }
    }
    const f32 gaps = shown > 1 ? static_cast<f32>(shown - 1) * spacing * scale : 0.0f;
    const f32 share = flexible > 0 ? std::max(0.0f, total - fixed - gaps) / static_cast<f32>(flexible) : 0.0f;
    f32 cursor = m_rect.min[along];
    for (usize i = 0; i < m_children.size(); ++i)
    {
        Widget& child = *m_children[i];
        if (!child.visible)
        {
            continue;
        }
        const LayoutBox& b = boxes[i];
        const f32 extent = b.size[along] > 0.0f ? b.size[along] * scale : share;
        // The slot along the stack; across it the child's own box (anchor, offset, size) applies.
        Rect slot = m_rect;
        slot.min[along] = cursor;
        slot.max[along] = cursor + extent;
        LayoutBox crossBox;
        crossBox.anchor = b.anchor;
        crossBox.offset[across] = b.offset[across];
        crossBox.size[across] = b.size[across];
        crossBox.size[along] = 0.0f;
        child.arrangeAt(place(crossBox, slot, scale), scale, layout);
        cursor += extent + spacing * scale;
    }
}

// --- List ---

void List::select(usize row)
{
    if (rows.empty())
    {
        m_selected = 0;
        return;
    }
    m_selected = std::min(row, rows.size() - 1);
    keepVisible();
}

void List::keepVisible()
{
    if (m_selected < m_first)
    {
        m_first = m_selected;
    }
    else if (m_selected >= m_first + m_visible)
    {
        m_first = m_selected + 1 - m_visible;
    }
    const usize maxFirst = rows.size() > m_visible ? rows.size() - m_visible : 0;
    m_first = std::min(m_first, maxFirst);
}

bool List::navigate(NavDir dir)
{
    if (dir == NavDir::Up && m_selected > 0)
    {
        select(m_selected - 1);
        return true;
    }
    if (dir == NavDir::Down && m_selected + 1 < rows.size())
    {
        select(m_selected + 1);
        return true;
    }
    return false;
}

bool List::accept()
{
    if (onAccept && m_selected < rows.size() && !isDimmed(m_selected))
    {
        onAccept(m_selected);
        return true;
    }
    return false;
}

bool List::scroll(f32 steps)
{
    const usize before = m_first;
    const auto delta = static_cast<long long>(std::lround(-steps));
    const usize maxFirst = rows.size() > m_visible ? rows.size() - m_visible : 0;
    const long long first =
        std::clamp(static_cast<long long>(m_first) + delta, 0LL, static_cast<long long>(maxFirst));
    m_first = static_cast<usize>(first);
    return m_first != before;
}

bool List::pointerAt(Vec2 point)
{
    if (rows.empty() || point.y < m_rect.min.y)
    {
        return false;
    }
    const auto row = m_first + static_cast<usize>((point.y - m_rect.min.y) / m_rowPixels);
    if (row >= rows.size() || row >= m_first + m_visible || row == m_selected)
    {
        return false;
    }
    m_selected = row;
    return true;
}

void List::arrangeChildren(f32 scale, const Layout* layout)
{
    m_rowPixels = std::max(1.0f, rowHeight * scale);
    m_visible = std::max<usize>(1, static_cast<usize>(m_rect.height() / m_rowPixels));
    if (!rows.empty())
    {
        m_selected = std::min(m_selected, rows.size() - 1);
    }
    keepVisible();
    Widget::arrangeChildren(scale, layout);
}

void List::drawSelf(const DrawContext& context) const
{
    const bool focused = context.focus == this;
    context.canvas.pushClip(m_rect);
    for (usize row = m_first; row < rows.size() && row < m_first + m_visible; ++row)
    {
        const Rect rect =
            Rect::fromSize(Vec2(m_rect.min.x, m_rect.min.y + static_cast<f32>(row - m_first) * m_rowPixels),
                           Vec2(m_rect.width(), m_rowPixels));
        const bool selected = row == m_selected;
        if (selected && focused)
        {
            drawFrame(context, rect, "highlight");
        }
        const bool lit = selected && focused;
        const bool dim = isDimmed(row) && !lit;
        drawCentredLine(context, rect, role, rows[row].resolve(context.ui.strings()), Align::Left, lit || dim,
                        10.0f, dim ? std::string_view("text_dim") : std::string_view(highlightColor));
        if (row < values.size())
        {
            drawCentredLine(context, rect, role, values[row].resolve(context.ui.strings()), Align::Right,
                            lit || dim, 10.0f,
                            dim ? std::string_view("text_dim") : std::string_view(highlightColor));
        }
    }
    context.canvas.popClip();
}

// --- Grid ---

usize Grid::rows() const noexcept
{
    return columns == 0 ? 0 : (count + columns - 1) / columns;
}

void Grid::select(usize index)
{
    m_selected = count == 0 ? 0 : std::min(index, count - 1);
    keepVisible();
}

void Grid::keepVisible()
{
    if (columns == 0)
    {
        return;
    }
    const usize row = m_selected / columns;
    if (row < m_firstRow)
    {
        m_firstRow = row;
    }
    else if (row >= m_firstRow + m_visibleRows)
    {
        m_firstRow = row + 1 - m_visibleRows;
    }
    const usize maxFirst = rows() > m_visibleRows ? rows() - m_visibleRows : 0;
    m_firstRow = std::min(m_firstRow, maxFirst);
}

Rect Grid::cellRect(usize index) const noexcept
{
    const usize column = columns == 0 ? 0 : index % columns;
    const usize row = columns == 0 ? 0 : index / columns;
    const f32 step = m_cellPixels + m_spacingPixels;
    const Vec2 at(m_rect.min.x + static_cast<f32>(column) * step,
                  m_rect.min.y + (static_cast<f32>(row) - static_cast<f32>(m_firstRow)) * step);
    return Rect::fromSize(at, Vec2(m_cellPixels));
}

bool Grid::navigate(NavDir dir)
{
    if (count == 0 || columns == 0)
    {
        return false;
    }
    const usize column = m_selected % columns;
    switch (dir)
    {
    case NavDir::Left:
        if (column == 0)
        {
            return false;
        }
        select(m_selected - 1);
        return true;
    case NavDir::Right:
        if (column + 1 >= columns || m_selected + 1 >= count)
        {
            return false;
        }
        select(m_selected + 1);
        return true;
    case NavDir::Up:
        if (m_selected < columns)
        {
            return false;
        }
        select(m_selected - columns);
        return true;
    case NavDir::Down:
        if (m_selected + columns < count)
        {
            select(m_selected + columns);
            return true;
        }
        if (m_selected / columns + 1 < rows()) // the last row is shorter: its last cell
        {
            select(count - 1);
            return true;
        }
        return false;
    case NavDir::None:
        break;
    }
    return false;
}

bool Grid::accept()
{
    if (onAccept && m_selected < count)
    {
        onAccept(m_selected);
        return true;
    }
    return false;
}

bool Grid::scroll(f32 steps)
{
    const usize before = m_firstRow;
    const auto delta = static_cast<long long>(std::lround(-steps));
    const usize maxFirst = rows() > m_visibleRows ? rows() - m_visibleRows : 0;
    m_firstRow = static_cast<usize>(
        std::clamp(static_cast<long long>(m_firstRow) + delta, 0LL, static_cast<long long>(maxFirst)));
    return m_firstRow != before;
}

bool Grid::pointerAt(Vec2 point)
{
    const f32 step = m_cellPixels + m_spacingPixels;
    if (count == 0 || columns == 0 || point.x < m_rect.min.x || point.y < m_rect.min.y || step <= 0.0f)
    {
        return false;
    }
    const auto column = static_cast<usize>((point.x - m_rect.min.x) / step);
    const auto row = m_firstRow + static_cast<usize>((point.y - m_rect.min.y) / step);
    const usize index = row * columns + column;
    if (column >= columns || index >= count || row >= m_firstRow + m_visibleRows || index == m_selected)
    {
        return false;
    }
    m_selected = index;
    return true;
}

void Grid::arrangeChildren(f32 scale, const Layout* layout)
{
    m_cellPixels = std::max(1.0f, cell * scale);
    m_spacingPixels = spacing * scale;
    m_visibleRows = std::max<usize>(
        1, static_cast<usize>((m_rect.height() + m_spacingPixels) / (m_cellPixels + m_spacingPixels)));
    if (count > 0)
    {
        m_selected = std::min(m_selected, count - 1);
    }
    keepVisible();
    Widget::arrangeChildren(scale, layout);
}

void Grid::drawSelf(const DrawContext& context) const
{
    const bool focused = context.focus == this;
    context.canvas.pushClip(m_rect);
    const usize first = m_firstRow * columns;
    const usize last = std::min(count, (m_firstRow + m_visibleRows) * columns);
    for (usize i = first; i < last; ++i)
    {
        const Rect rect = cellRect(i);
        const bool selected = focused && i == m_selected;
        if (drawCell)
        {
            drawCell(context, rect, i, selected);
        }
        else
        {
            drawFrame(context, rect, selected ? "cell_selected" : "cell");
        }
    }
    context.canvas.popClip();
}
} // namespace g7::ui
