// Character sheet, diary and map screens without a GPU (M14 part E).

#include <g7/core/FileSystem.hpp>
#include <g7/ui/JournalScreens.hpp>
#include <g7/ui/UiResources.hpp>

#include <doctest/doctest.h>

#include <ostream>
#include <string>

using namespace g7;
using namespace g7::ui;

namespace
{
const Rect kScreen{Vec2(0.0f), Vec2(1920.0f, 1080.0f)};

void run(Screen& screen, const UiInput& in = {})
{
    screen.update(&in, 0.016f, kScreen, 1.0f, nullptr);
}

UiInput tab(int delta)
{
    UiInput in;
    (delta > 0 ? in.nextTab : in.prevTab) = true;
    return in;
}

/// Draws a screen once with the shipped resources (no GPU): text layouts are computed.
void drawOnce(const Screen& screen)
{
    const fs::Path root = fs::fromUtf8(G7_ASSET_SOURCE_DIR);
    auto ui = UiResources::load([&](std::string_view path)
                                { return fs::readFile(root / fs::fromUtf8(path)); }, "de", nullptr);
    REQUIRE(ui);
    auto canvas = Canvas::create(nullptr, nullptr);
    REQUIRE(canvas);
    canvas.value().begin(kScreen.max, 1.0f);
    screen.draw(canvas.value(), ui.value());
    canvas.value().end(nullptr);
}
} // namespace

TEST_CASE("Journal screens: the character sheet shows its rows and closes")
{
    int closed = 0;
    StatusScreen screen(
        []
        {
            StatusView v;
            v.name = "Held";
            v.guild = "Bürger";
            v.general = {{"Stufe", "3"}};
            v.attributes = {{"Stärke", "15"}, {"Geschick", "12"}};
            return v;
        },
        [&] { ++closed; });
    run(screen);
    CHECK(screen.view().attributes.size() == 2);
    CHECK(screen.focus() == nullptr); // nothing to choose
    drawOnce(screen);
    UiInput back;
    back.back = true;
    run(screen, back);
    CHECK(closed == 1);
    CHECK(screen.closed());
}

TEST_CASE("Journal screens: the diary switches sections, topics and entries")
{
    DiaryView view;
    view.chapter = "Kapitel 1";
    view.running = {{"Arbeit im Lager", {"Eins.", "Zwei."}}, {"Essen", {"Drei."}}};
    view.notes = {{"Lager", {"Notiz."}}};
    DiaryScreen screen([&] { return view; }, [] {});
    run(screen);
    CHECK(screen.section() == 0);
    REQUIRE(screen.topics().rows.size() == 2);
    CHECK(screen.focus() == &screen.topics());
    CHECK(screen.entries().text == "Eins.\n\nZwei.");
    UiInput down;
    down.nav = NavDir::Down;
    run(screen, down);
    run(screen);
    CHECK(screen.entries().text == "Drei.");
    run(screen, tab(1)); // done: empty
    run(screen);
    CHECK(screen.section() == 1);
    CHECK_FALSE(screen.topics().visible);
    run(screen, tab(-1));
    run(screen, tab(-1)); // wraps to the notes
    run(screen);
    CHECK(screen.section() == 3);
    CHECK(screen.entries().text == "Notiz.");
    drawOnce(screen);
}

TEST_CASE("Journal screens: long diary entries scroll")
{
    DiaryView view;
    std::string text;
    for (int i = 0; i < 80; ++i)
    {
        text += "Ein langer Eintrag im Tagebuch, der viele Zeilen braucht. ";
    }
    view.running = {{"Lang", {text}}};
    DiaryScreen screen([&] { return view; }, [] {});
    run(screen);
    drawOnce(screen); // measures the text
    CHECK(screen.entries().scrollable());
    CHECK(screen.entries().scroll(-2.0f));
    CHECK(screen.entries().scrollOffset() > 0.0f);
    CHECK(screen.entries().navigate(NavDir::Up));
}

TEST_CASE("Journal screens: the map is fitted with its aspect ratio")
{
    int closed = 0;
    MapScreen screen("Karte", nullptr, Vec2(1200.0f, 800.0f), [&] { ++closed; });
    run(screen);
    const Rect picture = screen.imageRect();
    CHECK(picture.width() / picture.height() == doctest::Approx(1.5f));
    CHECK(picture.width() > 600.0f);
    UiInput accept;
    accept.accept = true;
    run(screen, accept);
    CHECK(closed == 1);
}
