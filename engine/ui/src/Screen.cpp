#include <g7/ui/Canvas.hpp>
#include <g7/ui/Screen.hpp>
#include <g7/ui/UiResources.hpp>

#include <algorithm>
#include <utility>

namespace g7::ui
{
void Screen::update(const UiInput* input, f32 deltaSeconds, const Rect& area, f32 scale, const Layout* layout)
{
    onUpdate(deltaSeconds);
    m_root.arrange(area, scale, layout);
    std::vector<Widget*> focusables;
    m_root.collectFocusables(focusables);
    if (std::find(focusables.begin(), focusables.end(), m_focus) == focusables.end())
    {
        m_focus = focusables.empty() ? nullptr : focusables.front();
    }
    if (input == nullptr)
    {
        return;
    }
    const UiInput& in = *input;

    // Mouse: hovering focuses (and selects rows or cells), a click accepts, the wheel scrolls what it is
    // over.
    if (in.pointerMoved || in.click || in.wheel != 0.0f)
    {
        Widget* hovered = m_root.hit(in.pointer);
        if (hovered != nullptr && (in.pointerMoved || in.click))
        {
            const bool moved = hovered != m_focus;
            m_focus = hovered;
            if (hovered->pointerAt(in.pointer) || moved)
            {
                emit("select");
            }
        }
        if (in.click && hovered != nullptr && hovered->accept())
        {
            emit("accept");
        }
        if (in.wheel != 0.0f)
        {
            Widget* target = hovered != nullptr ? hovered : m_focus;
            if (target != nullptr)
            {
                (void)target->scroll(in.wheel);
            }
        }
    }

    if (in.nav != NavDir::None && m_focus != nullptr)
    {
        if (m_focus->navigate(in.nav))
        {
            emit("select");
        }
        else
        {
            std::vector<Rect> rects;
            rects.reserve(focusables.size());
            for (const Widget* w : focusables)
            {
                rects.push_back(w->rect());
            }
            const auto current = static_cast<usize>(std::find(focusables.begin(), focusables.end(), m_focus) -
                                                    focusables.begin());
            if (const auto next = navigate(rects, current, in.nav))
            {
                m_focus = focusables[*next];
                emit("select");
            }
            else if (onDirection(in.nav))
            {
                emit("select");
            }
        }
    }
    else if (in.nav != NavDir::None && onDirection(in.nav))
    {
        emit("select");
    }
    if (in.accept && ((m_focus != nullptr && m_focus->accept()) || onAccept()))
    {
        emit("accept");
    }
    if ((in.prevTab && onTab(-1)) || (in.nextTab && onTab(1)))
    {
        emit("select");
    }
    if (in.back && onBack())
    {
        emit("back");
    }
}

void Screen::draw(Canvas& canvas, const UiResources& ui) const
{
    const DrawContext context{canvas, ui, m_focus};
    m_root.draw(context);
}

std::vector<std::string> Screen::takeEvents()
{
    return std::exchange(m_events, {});
}

void ScreenStack::push(std::unique_ptr<Screen> screen)
{
    if (screen)
    {
        m_screens.push_back(std::move(screen));
    }
}

void ScreenStack::pop()
{
    if (!m_screens.empty())
    {
        m_screens.pop_back();
    }
}

void ScreenStack::clear()
{
    m_screens.clear();
}

Screen* ScreenStack::find(std::string_view name) const noexcept
{
    for (const auto& screen : m_screens)
    {
        if (screen->name() == name)
        {
            return screen.get();
        }
    }
    return nullptr;
}

bool ScreenStack::blocksGameInput() const noexcept
{
    return std::any_of(m_screens.begin(), m_screens.end(),
                       [](const auto& s) { return s->flags().blocksGameInput; });
}

bool ScreenStack::pausesGame() const noexcept
{
    return std::any_of(m_screens.begin(), m_screens.end(),
                       [](const auto& s) { return s->flags().pausesGame; });
}

bool ScreenStack::hidesHud() const noexcept
{
    return std::any_of(m_screens.begin(), m_screens.end(), [](const auto& s) { return s->flags().hidesHud; });
}

void ScreenStack::update(const UiInput& input, f32 deltaSeconds, const Rect& area, f32 scale,
                         const Layout* layout)
{
    Screen* const topScreen = top();
    for (const auto& screen : m_screens)
    {
        screen->update(screen.get() == topScreen ? &input : nullptr, deltaSeconds, area, scale, layout);
    }
    for (const auto& screen : m_screens)
    {
        for (std::string& event : screen->takeEvents())
        {
            m_events.push_back(std::move(event));
        }
    }
    std::erase_if(m_screens, [](const auto& s) { return s->closed(); });
}

void ScreenStack::draw(Canvas& canvas, const UiResources& ui) const
{
    for (const auto& screen : m_screens)
    {
        screen->draw(canvas, ui);
    }
}

std::vector<std::string> ScreenStack::takeEvents()
{
    return std::exchange(m_events, {});
}
} // namespace g7::ui
