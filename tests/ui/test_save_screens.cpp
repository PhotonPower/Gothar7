// The save and load screen of M15 part D without a GPU: the slots as rows with their detail, unusable ones
// greyed out, the selected row reported (for its thumbnail), a question before overwriting (accept again:
// yes, back: no), back leaves.

#include <g7/ui/SaveScreens.hpp>

#include <doctest/doctest.h>

#include <ostream>
#include <string>
#include <vector>

using namespace g7;
using namespace g7::ui;

namespace
{
const Rect kScreen{Vec2(0.0f), Vec2(1920.0f, 1080.0f)};

void run(Screen& screen, UiInput in = {})
{
    screen.update(&in, 0.016f, kScreen, 1.0f, nullptr);
}

UiInput nav(NavDir dir)
{
    UiInput in;
    in.nav = dir;
    return in;
}

UiInput accept()
{
    UiInput in;
    in.accept = true;
    return in;
}

UiInput back()
{
    UiInput in;
    in.back = true;
    return in;
}
} // namespace

TEST_CASE("Save screen: rows, the selected one reported, a question before overwriting")
{
    std::vector<usize> selected;
    std::vector<usize> chosen;
    int closes = 0;
    SaveScreen::Actions actions;
    actions.view = []
    {
        SaveScreenView view;
        view.rows = {{"slot_01", "Lager – Kapitel 1", "01.11.2026 12:00", true},
                     {"slot_02", "– leer –", "", true},
                     {"quick", "– beschädigt –", "", false}};
        return view;
    };
    actions.select = [&](usize row) { selected.push_back(row); };
    actions.question = [](usize row) { return row == 0 ? std::string("Überschreiben?") : std::string(); };
    actions.choose = [&](usize row) { chosen.push_back(row); };
    actions.close = [&] { ++closes; };
    SaveScreen screen(SaveScreen::Mode::Save, actions);
    CHECK(screen.name() == "save");
    CHECK(screen.flags().pausesGame);
    run(screen);
    REQUIRE(screen.slots().rows.size() == 3);
    CHECK(screen.slots().rows[0].value == "Lager – Kapitel 1");
    CHECK(screen.slots().values[0].value == "01.11.2026 12:00");
    CHECK(screen.slots().isDimmed(2));
    CHECK(selected == std::vector<usize>{0});

    run(screen, accept()); // the question first
    CHECK(chosen.empty());
    CHECK(screen.asking() == "Überschreiben?");
    run(screen, back()); // no
    CHECK(screen.asking().empty());
    CHECK_FALSE(screen.closed());
    run(screen, accept());
    run(screen, accept()); // yes
    CHECK(chosen == std::vector<usize>{0});

    run(screen, nav(NavDir::Down)); // an empty slot: taken at once
    run(screen);
    CHECK(selected.back() == 1);
    run(screen, accept());
    CHECK(chosen == std::vector<usize>{0, 1});
    run(screen, nav(NavDir::Down));
    run(screen);
    run(screen, accept()); // the broken one cannot be taken
    CHECK(chosen.size() == 2);

    run(screen, back());
    CHECK(closes == 1);
    CHECK(screen.closed());
}
