// Widgets and screens without a GPU (M14 part B): layout of stacks, lists and grids, focus moves with
// keyboard steps and the mouse, accept and going back, the screen stack.

#include <g7/ui/Screen.hpp>
#include <g7/ui/Widget.hpp>

#include <doctest/doctest.h>

#include <memory>
#include <string>
#include <vector>

using namespace g7;
using namespace g7::ui;

namespace
{
const Rect kScreen{Vec2(0.0f), Vec2(1920.0f, 1080.0f)};

UiInput step(NavDir dir)
{
    UiInput in;
    in.nav = dir;
    return in;
}

UiInput pressAccept()
{
    UiInput in;
    in.accept = true;
    return in;
}

UiInput hover(Vec2 point, bool click = false)
{
    UiInput in;
    in.pointer = point;
    in.pointerMoved = true;
    in.click = click;
    return in;
}

/// A list on the left, a grid on the right and two buttons below - like the sample screen.
class TestScreen : public Screen
{
public:
    explicit TestScreen(ScreenFlags f = {}) : m_flags(f)
    {
        list = &root().add<List>("body", 40.0f);
        list->box = {Anchor::TopLeft, Vec2(100.0f, 100.0f), Vec2(400.0f, 200.0f)}; // 5 rows visible
        for (int i = 0; i < 8; ++i)
        {
            list->rows.push_back(UiText::literal("row " + std::to_string(i)));
        }
        list->onAccept = [this](usize row) { accepted = "row " + std::to_string(row); };
        grid = &root().add<Grid>(7, 3, 100.0f, 10.0f);
        grid->box = {Anchor::TopLeft, Vec2(800.0f, 100.0f), Vec2(330.0f, 210.0f)}; // 2 rows visible
        grid->onAccept = [this](usize cell) { accepted = "cell " + std::to_string(cell); };
        Stack& buttons = root().add<Stack>(Stack::Direction::Horizontal, 20.0f);
        buttons.box = {Anchor::TopLeft, Vec2(100.0f, 600.0f), Vec2(420.0f, 50.0f)};
        ok = &buttons.add<Button>(UiText::literal("OK"), [this] { accepted = "ok"; });
        cancel = &buttons.add<Button>(UiText::literal("Cancel"), [this] { close(); });
    }
    [[nodiscard]] ScreenFlags flags() const override { return m_flags; }
    [[nodiscard]] std::string_view name() const override { return "test"; }
    bool onTab(int delta) override
    {
        tabs += delta;
        return true;
    }

    List* list = nullptr;
    Grid* grid = nullptr;
    Button* ok = nullptr;
    Button* cancel = nullptr;
    std::string accepted;
    int tabs = 0;

private:
    ScreenFlags m_flags;
};
} // namespace

TEST_CASE("Widgets: a stack shares its length")
{
    Widget root;
    Stack& stack = root.add<Stack>(Stack::Direction::Vertical, 10.0f);
    stack.box = {Anchor::TopLeft, Vec2(0.0f), Vec2(200.0f, 300.0f)};
    Widget& fixed = stack.add<Widget>(LayoutBox{Anchor::TopLeft, Vec2(0.0f), Vec2(0.0f, 50.0f)});
    Widget& a = stack.add<Widget>();
    Widget& b = stack.add<Widget>(LayoutBox{Anchor::Center, Vec2(0.0f), Vec2(100.0f, 0.0f)});
    root.arrange(kScreen, 1.0f, nullptr);
    CHECK(fixed.rect() == Rect{Vec2(0.0f), Vec2(200.0f, 50.0f)});
    // 300 - 50 - 2 x 10 = 230 shared by two.
    CHECK(a.rect() == Rect{Vec2(0.0f, 60.0f), Vec2(200.0f, 175.0f)});
    CHECK(b.rect().min.y == doctest::Approx(185.0f));
    CHECK(b.rect().height() == doctest::Approx(115.0f));
    CHECK(b.rect().min.x == doctest::Approx(50.0f)); // centred across, 100 wide

    b.visible = false; // hidden children take no room
    root.arrange(kScreen, 1.0f, nullptr);
    CHECK(a.rect().height() == doctest::Approx(240.0f));
}

TEST_CASE("Widgets: a list moves, scrolls and keeps the selection visible")
{
    Widget root;
    List& list = root.add<List>("body", 40.0f);
    list.box = {Anchor::TopLeft, Vec2(0.0f), Vec2(300.0f, 120.0f)}; // 3 rows visible
    for (int i = 0; i < 6; ++i)
    {
        list.rows.push_back(UiText::literal(std::to_string(i)));
    }
    root.arrange(kScreen, 1.0f, nullptr);
    CHECK(list.visibleRows() == 3);
    CHECK_FALSE(list.navigate(NavDir::Up)); // the top: focus leaves
    for (int i = 0; i < 3; ++i)
    {
        CHECK(list.navigate(NavDir::Down));
    }
    CHECK(list.selected() == 3);
    CHECK(list.firstVisible() == 1); // scrolled along
    CHECK_FALSE(list.navigate(NavDir::Left));
    CHECK(list.scroll(-5.0f)); // wheel towards the user: down
    CHECK(list.firstVisible() == 3);
    CHECK_FALSE(list.scroll(-1.0f));
    CHECK(list.pointerAt(Vec2(10.0f, 85.0f))); // third visible row
    CHECK(list.selected() == 5);
    CHECK_FALSE(list.accept()); // no handler
    // At a larger scale fewer rows fit.
    root.arrange(kScreen, 2.0f, nullptr);
    CHECK(list.visibleRows() == 3); // 240 px / 80 px
}

TEST_CASE("Widgets: a grid moves within rows and to the shorter last row")
{
    Widget root;
    Grid& grid = root.add<Grid>(7, 3, 100.0f, 10.0f); // rows: 0 1 2 / 3 4 5 / 6
    grid.box = {Anchor::TopLeft, Vec2(0.0f), Vec2(330.0f, 210.0f)};
    root.arrange(kScreen, 1.0f, nullptr);
    CHECK(grid.cellRect(4) == Rect::fromSize(Vec2(110.0f, 110.0f), Vec2(100.0f)));
    CHECK(grid.navigate(NavDir::Right));
    CHECK(grid.navigate(NavDir::Right));
    CHECK_FALSE(grid.navigate(NavDir::Right)); // end of the row
    CHECK(grid.navigate(NavDir::Down));
    CHECK(grid.selected() == 5);
    CHECK(grid.navigate(NavDir::Down)); // no cell below 5: the last cell
    CHECK(grid.selected() == 6);
    CHECK(grid.firstRow() == 1); // two rows visible: scrolled
    CHECK_FALSE(grid.navigate(NavDir::Down));
    CHECK_FALSE(grid.navigate(NavDir::Left)); // first column
    CHECK(grid.navigate(NavDir::Up));
    CHECK(grid.selected() == 3);
    CHECK(grid.scroll(1.0f)); // up again
    CHECK(grid.firstRow() == 0);
    CHECK(grid.pointerAt(Vec2(230.0f, 20.0f)));
    CHECK(grid.selected() == 2);
    CHECK_FALSE(grid.pointerAt(Vec2(105.0f, 20.0f + 220.0f))); // below the visible rows
}

TEST_CASE("Screens: keyboard steps move inside widgets first, then between them")
{
    TestScreen screen;
    screen.update(nullptr, 0.0f, kScreen, 1.0f, nullptr);
    CHECK(screen.focus() == screen.list); // the first focusable

    for (int i = 0; i < 7; ++i)
    {
        UiInput in = step(NavDir::Down);
        screen.update(&in, 0.0f, kScreen, 1.0f, nullptr);
    }
    CHECK(screen.list->selected() == 7);
    UiInput down = step(NavDir::Down);
    screen.update(&down, 0.0f, kScreen, 1.0f, nullptr);
    CHECK(screen.focus() == screen.ok); // the list's end: on to the buttons below

    UiInput right = step(NavDir::Right);
    screen.update(&right, 0.0f, kScreen, 1.0f, nullptr);
    CHECK(screen.focus() == screen.cancel);
    UiInput up = step(NavDir::Up);
    screen.update(&up, 0.0f, kScreen, 1.0f, nullptr);
    CHECK(screen.focus() == screen.list);

    UiInput accept = pressAccept();
    screen.update(&accept, 0.0f, kScreen, 1.0f, nullptr);
    CHECK(screen.accepted == "row 7");
    const std::vector<std::string> events = screen.takeEvents();
    CHECK(events.back() == "accept");
    CHECK(std::count(events.begin(), events.end(), "select") == 10);

    UiInput tab;
    tab.nextTab = true;
    screen.update(&tab, 0.0f, kScreen, 1.0f, nullptr);
    CHECK(screen.tabs == 1);
}

TEST_CASE("Screens: the mouse focuses, selects and clicks")
{
    TestScreen screen;
    screen.update(nullptr, 0.0f, kScreen, 1.0f, nullptr);
    UiInput overCell = hover(Vec2(930.0f, 130.0f)); // grid cell 1
    screen.update(&overCell, 0.0f, kScreen, 1.0f, nullptr);
    CHECK(screen.focus() == screen.grid);
    CHECK(screen.grid->selected() == 1);
    UiInput click = hover(Vec2(930.0f, 130.0f), true);
    screen.update(&click, 0.0f, kScreen, 1.0f, nullptr);
    CHECK(screen.accepted == "cell 1");
    UiInput outside = hover(Vec2(5.0f, 5.0f), true);
    screen.update(&outside, 0.0f, kScreen, 1.0f, nullptr);
    CHECK(screen.focus() == screen.grid); // nothing there: focus stays
    UiInput onCancel = hover(screen.cancel->rect().min + Vec2(5.0f), true);
    screen.update(&onCancel, 0.0f, kScreen, 1.0f, nullptr);
    CHECK(screen.closed());
}

TEST_CASE("Screens: the stack")
{
    ScreenStack stack;
    CHECK_FALSE(stack.blocksGameInput());
    auto first = std::make_unique<TestScreen>(ScreenFlags{.blocksGameInput = false});
    TestScreen* bottom = first.get();
    stack.push(std::move(first));
    CHECK_FALSE(stack.blocksGameInput());
    auto second = std::make_unique<TestScreen>(ScreenFlags{.blocksGameInput = true, .pausesGame = true});
    TestScreen* topScreen = second.get();
    stack.push(std::move(second));
    CHECK(stack.blocksGameInput());
    CHECK(stack.pausesGame());
    CHECK_FALSE(stack.hidesHud());
    CHECK(stack.find("test") == bottom);

    // Only the top screen gets the input.
    stack.update(step(NavDir::Down), 0.0f, kScreen, 1.0f, nullptr);
    CHECK(topScreen->list->selected() == 1);
    CHECK(bottom->list->selected() == 0);
    CHECK(stack.takeEvents() == std::vector<std::string>{"select"});

    // Going back closes the top screen; the next update removes it.
    UiInput back;
    back.back = true;
    stack.update(back, 0.0f, kScreen, 1.0f, nullptr);
    CHECK(stack.size() == 1);
    CHECK(stack.top() == bottom);
    CHECK_FALSE(stack.pausesGame());
    CHECK(stack.takeEvents() == std::vector<std::string>{"back"});
    stack.clear();
    CHECK(stack.empty());
}
