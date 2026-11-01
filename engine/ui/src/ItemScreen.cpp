#include <g7/ui/ItemScreen.hpp>
#include <g7/ui/Layout.hpp>
#include <g7/ui/UiResources.hpp>

#include <algorithm>
#include <format>
#include <iterator>

namespace g7::ui
{
namespace
{
constexpr f32 kPad = 16.0f; ///< UI units inside frames

/// A grid whose columns, cell size and spacing come from layout.toml [inventory].
class ItemGrid : public Grid
{
public:
    ItemGrid() : Grid(0, 5, 104.0f, 6.0f) {}

protected:
    void arrangeChildren(f32 scale, const Layout* layout) override
    {
        if (layout != nullptr)
        {
            columns = static_cast<usize>(std::max(1.0f, layout->number("inventory.columns", 5.0f)));
            cell = layout->number("inventory.cell", 104.0f);
            spacing = layout->number("inventory.spacing", 6.0f);
        }
        Grid::arrangeChildren(scale, layout);
    }
};

void drawFrame(const DrawContext& context, const Rect& rect, std::string_view frame)
{
    context.canvas.frame(rect, context.ui.frame(frame, context.scale()), context.ui.frameTint(frame));
}

void drawLine(const DrawContext& context, std::string_view role, std::string_view text, Vec2 at, f32 width,
              Align align = Align::Left, std::optional<Color> color = {})
{
    const TextLayout layout =
        context.ui.layout(role, text, context.scale(), {.wrapWidth = width, .maxLines = 1, .align = align});
    TextStyle style = context.ui.textStyle(role, context.scale());
    if (color)
    {
        style.color = *color;
    }
    context.canvas.text(at, layout, style);
}
} // namespace

/// The tabs above the hero's grid: frame `tab_active` for the open one.
class ItemTabBar : public Widget
{
public:
    explicit ItemTabBar(const ItemScreen& screen) : m_screen(screen) {}
    [[nodiscard]] usize tabAt(Vec2 point) const noexcept
    {
        const usize count = std::max<usize>(1, m_screen.m_tabs.size());
        const f32 width = m_rect.width() / static_cast<f32>(count);
        return std::min(count - 1, static_cast<usize>(std::max(0.0f, point.x - m_rect.min.x) / width));
    }

protected:
    void drawSelf(const DrawContext& context) const override
    {
        const auto& tabs = m_screen.m_tabs;
        if (tabs.empty())
        {
            return;
        }
        const f32 width = m_rect.width() / static_cast<f32>(tabs.size());
        for (usize i = 0; i < tabs.size(); ++i)
        {
            const Rect rect{Vec2(m_rect.min.x + width * static_cast<f32>(i), m_rect.min.y),
                            Vec2(m_rect.min.x + width * static_cast<f32>(i + 1), m_rect.max.y)};
            const bool open = i == m_screen.m_tab;
            drawFrame(context, rect, open ? "tab_active" : "tab");
            const TextLayout title =
                context.ui.layout("small", context.ui.strings().get(tabs[i].key), context.scale(),
                                  {.wrapWidth = rect.width(), .maxLines = 1, .align = Align::Center});
            TextStyle style = context.ui.textStyle("small", context.scale());
            if (open)
            {
                style.color = context.ui.style().color("highlight");
            }
            context.canvas.text(Vec2(rect.min.x, rect.min.y + (rect.height() - title.size.y) * 0.5f), title,
                                style);
        }
    }

private:
    const ItemScreen& m_screen;
};

/// The info box: the selected item large (turning), its name and details, count and price.
class ItemInfoBox : public Widget
{
public:
    explicit ItemInfoBox(const ItemScreen& screen) : m_screen(screen) {}

protected:
    void drawSelf(const DrawContext& context) const override
    {
        drawFrame(context, m_rect, "tooltip");
        const ItemEntry* entry = m_screen.selected();
        if (entry == nullptr)
        {
            return;
        }
        const f32 s = context.scale();
        const f32 pad = kPad * s;
        const f32 side = m_rect.height() - 2.0f * pad;
        const Rect picture = Rect::fromSize(m_rect.min + Vec2(pad), Vec2(side));
        if (m_screen.m_actions.preview)
        {
            if (const auto image = m_screen.m_actions.preview(entry->item, true, true);
                image && image->texture)
            {
                context.canvas.image(picture, image->texture, image->uv, Color{}, true);
            }
        }
        const f32 left = picture.max.x + pad;
        const f32 width = m_rect.max.x - pad - left;
        f32 y = m_rect.min.y + pad;
        drawLine(context, "body", entry->name, Vec2(left, y), width, Align::Left,
                 context.ui.style().color("highlight"));
        y += context.ui.textSize("body", s) * 1.4f;
        const Strings& strings = context.ui.strings();
        std::vector<std::string> lines = entry->details;
        if (entry->count > 1)
        {
            lines.push_back(strings.format("UI_ITEM_COUNT", {{"n", std::to_string(entry->count)}}));
        }
        if (m_screen.m_mode == ItemScreenMode::Trade)
        {
            lines.push_back(
                strings.format("UI_ITEM_PRICE", {{"price", formatNumber(entry->price, strings.language())}}));
        }
        for (const std::string& line : lines)
        {
            drawLine(context, "small", line, Vec2(left, y), width);
            y += context.ui.textSize("small", s) * 1.35f;
            if (y > m_rect.max.y - pad)
            {
                break;
            }
        }
    }

private:
    const ItemScreen& m_screen;
};

/// The quantity choice (F5): the item, the amount of the maximum, the total price.
class ItemQuantityBox : public Widget
{
public:
    explicit ItemQuantityBox(const ItemScreen& screen) : m_screen(screen) {}

protected:
    void drawSelf(const DrawContext& context) const override
    {
        if (!m_screen.m_quantity)
        {
            return;
        }
        const auto& q = *m_screen.m_quantity;
        const f32 s = context.scale();
        const Strings& strings = context.ui.strings();
        drawFrame(context, m_rect, "tooltip");
        const f32 width = m_rect.width() - 2.0f * kPad * s;
        f32 y = m_rect.min.y + kPad * s;
        const f32 left = m_rect.min.x + kPad * s;
        drawLine(context, "body", q.entry.name, Vec2(left, y), width, Align::Center,
                 context.ui.style().color("highlight"));
        y += context.ui.textSize("body", s) * 1.5f;
        drawLine(context, "title", std::format("{} / {}", q.amount, q.max), Vec2(left, y), width,
                 Align::Center);
        y += context.ui.textSize("title", s) * 1.3f;
        if (m_screen.m_mode == ItemScreenMode::Trade)
        {
            drawLine(context, "body",
                     strings.format("UI_ITEM_TOTAL",
                                    {{"price", formatNumber(q.entry.price * static_cast<i64>(q.amount),
                                                            strings.language())}}),
                     Vec2(left, y), width, Align::Center);
            y += context.ui.textSize("body", s) * 1.4f;
        }
        drawLine(context, "small", strings.get("UI_QUANTITY_HINT"), Vec2(left, y), width, Align::Center);
    }

private:
    const ItemScreen& m_screen;
};

std::vector<ItemTab> itemTabs(const Layout& layout)
{
    std::vector<ItemTab> tabs;
    const Config& c = layout.config();
    const usize count = c.arraySize("inventory.tabs");
    for (usize i = 0; i < count; ++i)
    {
        const std::string base = std::format("inventory.tabs[{}].", i);
        ItemTab tab;
        tab.key = c.get<std::string>(base + "key", "");
        tab.categories = c.get<std::vector<std::string>>(base + "categories", {});
        if (!tab.key.empty())
        {
            tabs.push_back(std::move(tab));
        }
    }
    if (tabs.empty())
    {
        tabs = {{"UI_TAB_WEAPONS", {"melee_1h", "melee_2h", "bow", "crossbow", "ammo"}},
                {"UI_TAB_ARMOR", {"armor", "helmet"}},
                {"UI_TAB_MAGIC", {"rune", "scroll"}},
                {"UI_TAB_ARTIFACTS", {"ring", "amulet", "belt"}},
                {"UI_TAB_FOOD", {"food", "potion"}},
                {"UI_TAB_DOCUMENTS", {"document"}},
                {"UI_TAB_MISC", {"key", "torch", "misc"}}};
    }
    return tabs;
}

ItemScreen::ItemScreen(ItemScreenMode mode, Actions actions, std::vector<ItemTab> tabs)
    : m_mode(mode), m_actions(std::move(actions)), m_tabs(std::move(tabs))
{
    if (mode != ItemScreenMode::Inventory)
    {
        // Trading and containers start on everything (proposal): the other side rarely has the hero's
        // categories.
        m_tabs.insert(m_tabs.begin(), ItemTab{"UI_TAB_ALL", {}});
    }
    Widget& top = root();
    const auto pane =
        [&](const char* key, Anchor anchor, f32 offsetX, Label*& title, Grid*& grid, Label*& footer)
    {
        Panel& panel = top.add<Panel>("leather");
        panel.layoutKey = key;
        panel.box = {anchor, Vec2(offsetX, 0.0f), Vec2(600.0f, 880.0f)};
        title = &panel.add<Label>("title", UiText::literal(""), Align::Center);
        title->layoutKey = "inventory.title";
        title->box = {Anchor::Top, Vec2(0.0f, 22.0f), Vec2(-60.0f, 44.0f)};
        grid = &panel.add<ItemGrid>();
        grid->layoutKey = "inventory.grid";
        grid->box = {Anchor::Top, Vec2(0.0f, 130.0f), Vec2(544.0f, 434.0f)};
        footer = &panel.add<Label>("small", UiText::literal(""));
        footer->layoutKey = "inventory.footer";
        footer->box = {Anchor::BottomLeft, Vec2(30.0f, -24.0f), Vec2(300.0f, 28.0f)};
        return &panel;
    };
    m_otherPanel = pane("inventory.other", Anchor::Left, 40.0f, m_otherTitle, m_otherGrid, m_otherFooter);
    Panel* hero = pane("inventory.hero", Anchor::Right, -40.0f, m_heroTitle, m_heroGrid, m_heroFooter);
    m_otherPanel->visible = mode != ItemScreenMode::Inventory;

    ItemTabBar& bar = hero->add<ItemTabBar>(*this);
    bar.layoutKey = "inventory.tab_bar";
    bar.box = {Anchor::Top, Vec2(0.0f, 76.0f), Vec2(-40.0f, 44.0f)};
    m_tabBar = &bar;
    ItemInfoBox& info = hero->add<ItemInfoBox>(*this);
    info.layoutKey = "inventory.info";
    info.box = {Anchor::Bottom, Vec2(0.0f, -64.0f), Vec2(-40.0f, 240.0f)};
    m_info = &info;
    m_message = &hero->add<Label>("small", UiText::literal(""), Align::Right);
    m_message->layoutKey = "inventory.message";
    m_message->box = {Anchor::BottomRight, Vec2(-30.0f, -24.0f), Vec2(300.0f, 28.0f)};
    ItemQuantityBox& quantity = top.add<ItemQuantityBox>(*this);
    quantity.layoutKey = "inventory.quantity";
    quantity.box = {Anchor::Center, Vec2(0.0f), Vec2(520.0f, 220.0f)};
    m_quantityPanel = &quantity;

    m_heroGrid->drawCell = [this](const DrawContext& context, const Rect& rect, usize index, bool selected)
    {
        if (index < m_heroItems.size())
        {
            drawCell(context, rect, m_heroItems[index], selected);
        }
    };
    m_otherGrid->drawCell = [this](const DrawContext& context, const Rect& rect, usize index, bool selected)
    {
        if (index < m_otherItems.size())
        {
            drawCell(context, rect, m_otherItems[index], selected);
        }
    };
    m_heroGrid->onAccept = [this](usize index)
    {
        if (index >= m_heroItems.size())
        {
            return;
        }
        if (m_mode == ItemScreenMode::Inventory)
        {
            if (m_actions.use)
            {
                m_actions.use(m_heroItems[index]);
            }
        }
        else
        {
            startTransfer(false, m_heroItems[index]);
        }
    };
    m_otherGrid->onAccept = [this](usize index)
    {
        if (index < m_otherItems.size())
        {
            startTransfer(true, m_otherItems[index]);
        }
    };
}

std::string_view ItemScreen::name() const
{
    switch (m_mode)
    {
    case ItemScreenMode::Trade:
        return "trade";
    case ItemScreenMode::Container:
        return "container";
    case ItemScreenMode::Inventory:
        break;
    }
    return "inventory";
}

const ItemEntry* ItemScreen::selected() const noexcept
{
    if (selectedInOther())
    {
        return &m_otherItems[m_otherGrid->selected()];
    }
    return m_heroGrid->selected() < m_heroItems.size() ? &m_heroItems[m_heroGrid->selected()] : nullptr;
}

bool ItemScreen::selectedInOther() const noexcept
{
    return m_mode != ItemScreenMode::Inventory && focus() == m_otherGrid &&
           m_otherGrid->selected() < m_otherItems.size();
}

void ItemScreen::filter()
{
    const std::vector<std::string>* categories =
        m_tab < m_tabs.size() && !m_tabs[m_tab].categories.empty() ? &m_tabs[m_tab].categories : nullptr;
    const auto keep = [&](const ItemEntry& e)
    {
        return categories == nullptr ||
               std::find(categories->begin(), categories->end(), e.category) != categories->end();
    };
    m_heroItems.clear();
    std::copy_if(m_view.hero.items.begin(), m_view.hero.items.end(), std::back_inserter(m_heroItems), keep);
    m_otherItems.clear();
    std::copy_if(m_view.other.items.begin(), m_view.other.items.end(), std::back_inserter(m_otherItems),
                 keep);
    m_heroGrid->count = m_heroItems.size();
    m_otherGrid->count = m_otherItems.size();
}

void ItemScreen::onUpdate(f32)
{
    if (m_actions.view)
    {
        m_view = m_actions.view();
    }
    if (!m_tabChosen)
    {
        // The inventory opens on the first tab with something in it (proposal).
        m_tabChosen = true;
        for (usize i = 0; i < m_tabs.size() && m_mode == ItemScreenMode::Inventory; ++i)
        {
            m_tab = i;
            filter();
            if (!m_heroItems.empty())
            {
                break;
            }
        }
        if (m_mode == ItemScreenMode::Inventory && m_heroItems.empty())
        {
            m_tab = 0;
        }
    }
    filter();
    m_heroTitle->text = UiText::literal(m_view.hero.title);
    m_otherTitle->text = UiText::literal(m_view.other.title);
    m_heroFooter->text = UiText::literal(m_view.hero.footer);
    m_otherFooter->text = UiText::literal(m_view.other.footer);
    m_message->text = UiText::literal(m_view.message);
    m_quantityPanel->visible = m_quantity.has_value();
}

void ItemScreen::startTransfer(bool fromOther, const ItemEntry& entry)
{
    if (!m_actions.transfer)
    {
        return;
    }
    const u32 max = m_actions.maxTransfer ? m_actions.maxTransfer(fromOther, entry) : entry.count;
    if (max <= 1)
    {
        m_actions.transfer(fromOther, entry, 1); // also when nothing is possible: the engine says why
        return;
    }
    // F5: a stack asks how many - trading starts at one, a container at all of it (proposal).
    m_quantity = Quantity{fromOther, entry, m_mode == ItemScreenMode::Container ? max : 1u, max};
    m_quantityPanel->visible = true;
}

bool ItemScreen::onInput(const UiInput& in)
{
    if (m_quantity)
    {
        Quantity& q = *m_quantity;
        const auto change = [&](i64 delta)
        { q.amount = static_cast<u32>(std::clamp<i64>(static_cast<i64>(q.amount) + delta, 1, q.max)); };
        if (in.nav == NavDir::Left || in.nav == NavDir::Down)
        {
            change(-1);
        }
        else if (in.nav == NavDir::Right || in.nav == NavDir::Up)
        {
            change(1);
        }
        if (in.prevTab)
        {
            change(-10);
        }
        if (in.nextTab)
        {
            change(10);
        }
        if (in.wheel != 0.0f)
        {
            change(in.wheel > 0.0f ? 1 : -1);
        }
        if (in.accept || in.click)
        {
            const Quantity chosen = q;
            m_quantity.reset();
            m_actions.transfer(chosen.fromOther, chosen.entry, chosen.amount);
            emit("accept");
        }
        else if (in.back)
        {
            m_quantity.reset();
            emit("back");
        }
        return true;
    }
    const Rect& bar = m_tabBar->rect();
    if (in.click && in.pointer.x >= bar.min.x && in.pointer.x < bar.max.x && in.pointer.y >= bar.min.y &&
        in.pointer.y < bar.max.y)
    {
        m_tab = static_cast<const ItemTabBar*>(m_tabBar)->tabAt(in.pointer);
        filter();
        emit("select");
        return true;
    }
    if (in.drop && m_mode == ItemScreenMode::Inventory && m_actions.drop)
    {
        if (const ItemEntry* entry = selected())
        {
            m_actions.drop(*entry);
            emit("accept");
        }
        return true;
    }
    return false;
}

bool ItemScreen::onTab(int delta)
{
    if (m_tabs.empty())
    {
        return false;
    }
    const auto count = static_cast<int>(m_tabs.size());
    m_tab = static_cast<usize>(((static_cast<int>(m_tab) + delta) % count + count) % count);
    filter();
    m_heroGrid->select(0);
    m_otherGrid->select(0);
    return true;
}

bool ItemScreen::onBack()
{
    if (m_actions.close)
    {
        m_actions.close();
    }
    close();
    return true;
}

void ItemScreen::drawCell(const DrawContext& context, const Rect& rect, const ItemEntry& entry,
                          bool selected) const
{
    drawFrame(context, rect, selected ? "cell_selected" : entry.equipped ? "cell_equipped" : "cell");
    if (m_actions.preview)
    {
        if (const auto image = m_actions.preview(entry.item, false, selected); image && image->texture)
        {
            const f32 inset = 4.0f * context.scale();
            context.canvas.image({rect.min + Vec2(inset), rect.max - Vec2(inset)}, image->texture, image->uv,
                                 Color{}, true);
        }
    }
    if (entry.count > 1)
    {
        const f32 s = context.scale();
        const TextLayout count =
            context.ui.layout("small", std::to_string(entry.count), s,
                              {.wrapWidth = rect.width() - 10.0f * s, .maxLines = 1, .align = Align::Right});
        context.canvas.text(Vec2(rect.min.x + 4.0f * s, rect.max.y - count.size.y - 4.0f * s), count,
                            context.ui.textStyle("small", s));
    }
}
} // namespace g7::ui
