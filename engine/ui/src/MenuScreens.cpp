#include <g7/platform/Actions.hpp>
#include <g7/platform/Input.hpp>
#include <g7/ui/MenuScreens.hpp>

#include <algorithm>
#include <cmath>
#include <format>

namespace g7::ui
{
// --- Main and pause menu ---

MenuScreen::MenuScreen(std::vector<MenuEntry> entries, Actions actions, bool title)
    : m_entries(std::move(entries)), m_actions(std::move(actions))
{
    if (title)
    {
        Label& heading = root().add<Label>("menu_title", UiText::key("UI_MENU_TITLE"), Align::Center);
        heading.layoutKey = "menu.title";
        heading.box = {Anchor::Top, Vec2(0.0f, 120.0f), Vec2(1000.0f, 120.0f)};
    }
    Panel& panel = root().add<Panel>("panel");
    panel.layoutKey = "menu.box";
    panel.box = {Anchor::Center, Vec2(0.0f, 80.0f), Vec2(460.0f, 420.0f)};
    panel.heightOverride = 52.0f * static_cast<f32>(m_entries.size()) + 60.0f; // as high as the entries
    List& list = panel.add<List>("menu", 52.0f);
    list.layoutKey = "menu.list";
    list.box = {Anchor::Center, Vec2(0.0f), Vec2(-60.0f, -50.0f)};
    for (const MenuEntry& entry : m_entries)
    {
        list.rows.push_back(UiText::key(entry.textKey));
        list.dimmed.push_back(!entry.enabled);
    }
    list.onAccept = [this](usize row)
    {
        if (m_actions.choose && row < m_entries.size())
        {
            m_actions.choose(m_entries[row].id);
        }
    };
    m_list = &list;
    setFocus(&list);
}

void MenuScreen::select(std::string_view id)
{
    for (usize row = 0; row < m_entries.size(); ++row)
    {
        if (m_entries[row].id == id)
        {
            m_list->select(row);
        }
    }
}

bool MenuScreen::onBack()
{
    if (!m_actions.back)
    {
        return false; // the main menu stays
    }
    m_actions.back();
    return true;
}

// --- Options ---

OptionValue stepOption(const OptionDef& option, const OptionValue& current, int direction)
{
    switch (option.kind)
    {
    case OptionKind::Slider:
    {
        const f64 value = std::holds_alternative<f64>(current)   ? std::get<f64>(current)
                          : std::holds_alternative<i64>(current) ? static_cast<f64>(std::get<i64>(current))
                                                                 : option.min;
        const f64 step = option.step > 0.0 ? option.step : 0.05;
        // Snap to the step grid, so 0.8 + 0.05 stays 0.85 and not 0.8500000001.
        const f64 steps = std::round((value - option.min) / step) + static_cast<f64>(direction);
        return std::clamp(option.min + steps * step, option.min, option.max);
    }
    case OptionKind::Toggle:
        return std::holds_alternative<bool>(current) ? !std::get<bool>(current) : true;
    case OptionKind::Choice:
    {
        if (option.choices.empty())
        {
            return current;
        }
        const auto count = static_cast<long long>(option.choices.size());
        long long at = -1;
        for (long long i = 0; i < count; ++i)
        {
            if (option.choices[static_cast<usize>(i)].value == current)
            {
                at = i;
            }
        }
        // An unknown value (set by hand in the config) starts at the first or last choice.
        const long long next =
            at < 0 ? (direction > 0 ? 0 : count - 1) : ((at + direction) % count + count) % count;
        return option.choices[static_cast<usize>(next)].value;
    }
    case OptionKind::Binding:
        break;
    }
    return current;
}

UiText optionText(const OptionDef& option, const OptionValue& value)
{
    switch (option.kind)
    {
    case OptionKind::Slider:
    {
        const f64 v = std::holds_alternative<f64>(value)   ? std::get<f64>(value)
                      : std::holds_alternative<i64>(value) ? static_cast<f64>(std::get<i64>(value))
                                                           : 0.0;
        return UiText::literal(std::format("{} %", static_cast<int>(std::lround(v * 100.0))));
    }
    case OptionKind::Toggle:
        return UiText::key(std::holds_alternative<bool>(value) && std::get<bool>(value) ? "UI_ON" : "UI_OFF");
    case OptionKind::Choice:
        for (const OptionChoice& choice : option.choices)
        {
            if (choice.value == value)
            {
                return choice.label;
            }
        }
        return std::visit(
            [](const auto& v) -> UiText
            {
                using T = std::decay_t<decltype(v)>;
                if constexpr (std::is_same_v<T, std::string>)
                {
                    return UiText::literal(v);
                }
                else
                {
                    return UiText::literal(std::format("{}", v));
                }
            },
            value);
    case OptionKind::Binding:
        break;
    }
    return UiText::literal("");
}

std::vector<platform::Action> assignKey(platform::ActionMap& map, platform::Action action, platform::Key key,
                                        std::span<const platform::Action> group)
{
    using platform::InputBinding;
    std::vector<platform::Action> displaced;
    for (const platform::Action other : group)
    {
        if (other == action)
        {
            continue;
        }
        const auto bound = map.bindings(other);
        if (std::find(bound.begin(), bound.end(), InputBinding{key}) == bound.end())
        {
            continue;
        }
        std::vector<InputBinding> kept;
        for (const InputBinding& b : bound)
        {
            if (b != InputBinding{key})
            {
                kept.push_back(b);
            }
        }
        map.clear(other);
        for (const InputBinding& b : kept)
        {
            map.bind(other, b);
        }
        displaced.push_back(other);
    }
    // The new key first; the old first key goes, the rest stays in its order.
    const auto bound = map.bindings(action);
    std::vector<InputBinding> next{InputBinding{key}};
    bool replaced = false;
    for (const InputBinding& b : bound)
    {
        if (b == InputBinding{key})
        {
            continue;
        }
        if (!replaced && std::holds_alternative<platform::Key>(b))
        {
            replaced = true; // the previous first key
            continue;
        }
        next.push_back(b);
    }
    map.clear(action);
    for (const InputBinding& b : next)
    {
        map.bind(action, b);
    }
    return displaced;
}

OptionsScreen::OptionsScreen(std::vector<OptionPage> pages, Actions actions)
    : m_pages(std::move(pages)), m_actions(std::move(actions))
{
    Panel& panel = root().add<Panel>("panel");
    panel.layoutKey = "options.box";
    panel.box = {Anchor::Center, Vec2(0.0f), Vec2(1000.0f, 760.0f)};
    Label& title = panel.add<Label>("title", UiText::key("UI_OPTIONS_TITLE"), Align::Center);
    title.layoutKey = "options.title";
    title.box = {Anchor::Top, Vec2(0.0f, 30.0f), Vec2(-80.0f, 56.0f)};
    Widget& tabBar = panel.add<Widget>();
    tabBar.layoutKey = "options.tab_bar";
    tabBar.box = {Anchor::Top, Vec2(0.0f, 100.0f), Vec2(-80.0f, 48.0f)};
    const f32 tabWidth = 200.0f;
    const f32 left = -0.5f * tabWidth * static_cast<f32>(m_pages.size() - 1);
    for (usize i = 0; i < m_pages.size(); ++i)
    {
        Panel& tab = tabBar.add<Panel>("tab");
        tab.box = {Anchor::Center, Vec2(left + tabWidth * static_cast<f32>(i), 0.0f),
                   Vec2(tabWidth - 8.0f, 0.0f)};
        Label& label = tab.add<Label>("body", UiText::key(m_pages[i].title), Align::Center);
        label.box = {Anchor::Center, Vec2(0.0f), Vec2(0.0f)};
        m_tabFrames.push_back(&tab);
        m_tabs.push_back(&label);
    }
    List& list = panel.add<List>("body", 44.0f);
    list.layoutKey = "options.list";
    list.box = {Anchor::Top, Vec2(0.0f, 170.0f), Vec2(-120.0f, 470.0f)};
    list.onAccept = [this](usize row)
    {
        const OptionDef* def = option(row);
        if (def == nullptr)
        {
            return;
        }
        if (def->kind == OptionKind::Binding)
        {
            m_waiting = row;
            m_message.clear();
        }
        else
        {
            change(row, +1);
        }
    };
    m_list = &list;
    m_hint = &panel.add<Label>("small", UiText::key("UI_OPTIONS_HINT"), Align::Center);
    m_hint->layoutKey = "options.hint";
    m_hint->box = {Anchor::Bottom, Vec2(0.0f, -30.0f), Vec2(-80.0f, 60.0f)};
    m_hint->maxLines = 2;
    showPage(0);
    setFocus(&list);
}

const OptionDef* OptionsScreen::option(usize row) const
{
    if (m_page >= m_pages.size() || row >= m_pages[m_page].options.size())
    {
        return nullptr;
    }
    return &m_pages[m_page].options[row];
}

const OptionDef* OptionsScreen::waiting() const noexcept
{
    return m_waiting ? option(*m_waiting) : nullptr;
}

void OptionsScreen::showPage(usize page)
{
    m_page = std::min(page, m_pages.empty() ? usize{0} : m_pages.size() - 1);
    m_waiting.reset();
    m_message.clear();
    m_list->rows.clear();
    m_list->values.clear();
    if (m_page < m_pages.size())
    {
        for (const OptionDef& def : m_pages[m_page].options)
        {
            m_list->rows.push_back(UiText::key(def.label));
            m_list->values.push_back(UiText::literal(""));
        }
    }
    m_list->select(0);
    for (usize i = 0; i < m_tabs.size(); ++i)
    {
        m_tabFrames[i]->frame = i == m_page ? "tab_active" : "tab";
        m_tabs[i]->role = i == m_page ? "heading" : "body";
    }
}

void OptionsScreen::onUpdate(f32)
{
    for (usize row = 0; row < m_list->values.size(); ++row)
    {
        const OptionDef* def = option(row);
        if (def == nullptr)
        {
            continue;
        }
        if (def->kind == OptionKind::Binding)
        {
            m_list->values[row] = m_waiting == row
                                      ? UiText::key("UI_OPTIONS_PRESS_KEY")
                                      : UiText::literal(m_actions.binding ? m_actions.binding(*def) : "");
        }
        else if (m_actions.get)
        {
            m_list->values[row] = optionText(*def, m_actions.get(*def));
        }
    }
    m_hint->text = m_message.empty() ? UiText::key("UI_OPTIONS_HINT") : UiText::literal(m_message);
}

bool OptionsScreen::onInput(const UiInput& input)
{
    if (!m_waiting)
    {
        return false;
    }
    // Waiting for a key: everything else is swallowed; Esc cancels, the mouse cannot be assigned here.
    if (input.key)
    {
        const OptionDef* def = option(*m_waiting);
        if (*input.key != platform::Key::Escape && def != nullptr && m_actions.assign)
        {
            m_message = m_actions.assign(*def, *input.key);
            emit("accept");
        }
        m_waiting.reset();
    }
    else if (input.back)
    {
        m_waiting.reset(); // the gamepad's back button
    }
    return true;
}

void OptionsScreen::change(usize row, int direction)
{
    const OptionDef* def = option(row);
    if (def == nullptr || def->kind == OptionKind::Binding || !m_actions.get || !m_actions.set)
    {
        return;
    }
    m_actions.set(*def, stepOption(*def, m_actions.get(*def), direction));
}

bool OptionsScreen::onDirection(NavDir dir)
{
    if (dir != NavDir::Left && dir != NavDir::Right)
    {
        return false;
    }
    change(m_list->selected(), dir == NavDir::Right ? +1 : -1);
    return true;
}

bool OptionsScreen::onTab(int direction)
{
    if (m_pages.size() < 2)
    {
        return false;
    }
    const auto count = static_cast<long long>(m_pages.size());
    showPage(static_cast<usize>(((static_cast<long long>(m_page) + direction) % count + count) % count));
    return true;
}

bool OptionsScreen::onBack()
{
    close();
    if (m_actions.close)
    {
        m_actions.close();
    }
    return true;
}

// --- Loading screen ---

LoadingScreen::LoadingScreen(std::function<LoadingView()> view) : m_view(std::move(view))
{
    Panel& background = root().add<Panel>("bg_leather");
    background.layoutKey = "loading.background";
    background.box = {Anchor::Center, Vec2(0.0f), Vec2(0.0f)};
    m_title = &background.add<Label>("title", UiText::literal(""), Align::Center);
    m_title->layoutKey = "loading.title";
    m_title->box = {Anchor::Center, Vec2(0.0f, -40.0f), Vec2(1200.0f, 60.0f)};
    m_bar = &background.add<Bar>("", "highlight", 0.0f);
    m_bar->layoutKey = "loading.bar";
    m_bar->box = {Anchor::Center, Vec2(0.0f, 40.0f), Vec2(520.0f, 24.0f)};
}

void LoadingScreen::onUpdate(f32)
{
    const LoadingView view = m_view ? m_view() : LoadingView{};
    m_title->text = UiText::key("UI_LOADING", {{"world", view.world}});
    m_bar->fraction = std::clamp(view.progress, 0.0f, 1.0f);
}
} // namespace g7::ui
