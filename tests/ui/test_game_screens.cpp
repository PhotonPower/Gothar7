// HUD and the game screens of M14 part C without a GPU: messages, subtitles, HUD layout, dialogue, document,
// mob choice and lockpicking driven by UiInput, with fake engine callbacks.

#include <g7/core/FileSystem.hpp>
#include <g7/ui/GameScreens.hpp>
#include <g7/ui/Hud.hpp>
#include <g7/ui/Layout.hpp>
#include <g7/ui/UiResources.hpp>

#include <doctest/doctest.h>

#include <cmath>
#include <ostream>
#include <span>
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

void run(Screen& screen, const UiInput& in)
{
    screen.update(&in, 0.016f, kScreen, 1.0f, nullptr);
}

Result<UiResources> shippedUi()
{
    const fs::Path root = fs::fromUtf8(G7_ASSET_SOURCE_DIR);
    return UiResources::load([&](std::string_view path) { return fs::readFile(root / fs::fromUtf8(path)); },
                             "de", nullptr);
}
} // namespace

TEST_CASE("HUD: messages and subtitles")
{
    MessageQueue messages;
    for (int i = 0; i < 6; ++i)
    {
        messages.push("m" + std::to_string(i), 3.0f);
    }
    REQUIRE(messages.messages().size() == MessageQueue::kMaxShown);
    CHECK(messages.messages().front().text == "m2"); // the oldest went
    messages.update(2.0f);
    CHECK(messages.messages().size() == 4);
    messages.push("late", 3.0f);
    messages.update(1.5f);
    REQUIRE(messages.messages().size() == 1); // 3 s are over for the others
    CHECK(messages.messages().front().text == "late");

    SubtitleQueue subtitles;
    subtitles.push("Torwache", "Halt!", 2.0f);
    subtitles.push("Bauer", "Hilfe!", 4.0f);
    subtitles.push("Torwache", "Stehen bleiben!", 2.0f); // replaces the guard's line
    REQUIRE(subtitles.lines().size() == 2);
    CHECK(subtitles.lines().back().text == "Stehen bleiben!");
    subtitles.update(3.0f);
    REQUIRE(subtitles.lines().size() == 1);
    CHECK(subtitles.lines().front().speaker == "Bauer");

    CHECK(readingSeconds("Halt!") == doctest::Approx(2.0f)); // at least two seconds
    CHECK(readingSeconds(std::string(100, 'a')) == doctest::Approx(7.5f));
    CHECK(readingSeconds(std::string(1000, 'a')) == doctest::Approx(10.0f));
    CHECK(readingSeconds("\xC3\xA4\xC3\xA4") == readingSeconds("aa")); // characters, not bytes
}

TEST_CASE("HUD: places the bars as in Gothic 1 and draws what the model has")
{
    auto resources = shippedUi();
    REQUIRE_MESSAGE(resources, (resources ? "" : resources.error().message));
    const UiResources& ui = resources.value();

    Hud hud;
    HudModel model;
    model.health = {30.0f, 40.0f};
    MessageQueue messages;
    SubtitleQueue subtitles;
    hud.update(model, messages, std::span<const SubtitleQueue::Line>{}, kScreen, 1.0f, &ui.screenLayout());
    // F3: health bottom left, mana bottom right, enemy top centre (layout.toml).
    CHECK(hud.healthRect().min.x < 100.0f);
    CHECK(hud.healthRect().max.y > 1000.0f);
    CHECK(hud.manaRect().max.x > 1800.0f);
    CHECK(hud.enemyRect().min.y < 100.0f);
    CHECK(std::abs((hud.enemyRect().min.x + hud.enemyRect().max.x) * 0.5f - 960.0f) < 1.0f);
    CHECK(hud.subtitleRect().max.y > 900.0f);

    auto canvas = Canvas::create(nullptr, nullptr);
    REQUIRE(canvas);
    Canvas& c = canvas.value();
    c.begin(kScreen.max, 1.0f);
    hud.draw(c, ui);
    const usize healthOnly = c.vertices().size();
    CHECK(healthOnly > 0);
    c.end(nullptr);

    // Mana (only with magic), an enemy and air add bars; a hidden HUD draws no bars.
    model.mana = BarValue{10.0f, 20.0f};
    model.enemy = HudModel::Enemy{"Wolf", {5.0f, 10.0f}};
    model.air = 0.5f;
    hud.update(model, messages, std::span<const SubtitleQueue::Line>{}, kScreen, 1.0f, &ui.screenLayout());
    c.begin(kScreen.max, 1.0f);
    hud.draw(c, ui);
    CHECK(c.vertices().size() > healthOnly * 3);
    c.end(nullptr);
    model.visible = false;
    hud.update(model, messages, std::span<const SubtitleQueue::Line>{}, kScreen, 1.0f, &ui.screenLayout());
    c.begin(kScreen.max, 1.0f);
    hud.draw(c, ui);
    CHECK(c.vertices().empty());
    c.end(nullptr);
    CHECK(BarValue{50.0f, 40.0f}.fraction() == 1.0f);
    CHECK(BarValue{5.0f, 0.0f}.fraction() == 0.0f);
}

TEST_CASE("Game screens: the dialogue skips lines and chooses options")
{
    DialogView view{.menu = false, .options = {}, .selected = 0};
    int skips = 0;
    std::vector<usize> chosen;
    usize selected = 0;
    DialogScreen screen({.view = [&] { return view; },
                         .skip = [&] { ++skips; },
                         .choose = [&](usize i) { chosen.push_back(i); },
                         .select = [&](usize i) { selected = i; }});
    CHECK(screen.flags().blocksGameInput);
    CHECK(screen.flags().hidesHud);

    run(screen, accept()); // a line is spoken: accept skips it
    CHECK(skips == 1);
    CHECK(chosen.empty());

    view = {.menu = true, .options = {"Wer bist du?", "Ende"}, .selected = 0};
    run(screen, UiInput{});
    CHECK(screen.focus() == &screen.options());
    run(screen, step(NavDir::Down));
    run(screen, UiInput{});
    CHECK(selected == 1); // told to the engine
    run(screen, accept());
    CHECK(chosen == std::vector<usize>{1});
    CHECK(skips == 1);

    run(screen, back()); // a conversation is not left with Esc
    CHECK_FALSE(screen.closed());

    view.hidden = true; // trading: nothing shown, accept not taken
    view.menu = false;
    run(screen, accept());
    CHECK(skips == 1);
}

TEST_CASE("Game screens: document, choice and lockpicking")
{
    int closed = 0;
    DocumentScreen document("Brief", "Werter Meister ...", [&] { ++closed; });
    run(document, accept());
    CHECK(document.closed());
    CHECK(closed == 1);

    std::vector<usize> chosen;
    int left = 0;
    ChoiceScreen choice({.view = [] { return ChoiceView{"Amboss", {"Grobes Schwert", "Axt"}, ""}; },
                         .choose = [&](usize i) { chosen.push_back(i); },
                         .leave = [&] { ++left; }});
    run(choice, UiInput{});
    run(choice, step(NavDir::Down));
    run(choice, accept());
    CHECK(chosen == std::vector<usize>{1});
    run(choice, back());
    CHECK(left == 1);
    CHECK_FALSE(choice.closed()); // the engine closes it when the hero has left the mob

    std::vector<bool> turns;
    int stopped = 0;
    LockpickScreen lockpick({.view = [] { return LockpickView{"Truhe", 2, 5, 3, ""}; },
                             .turn = [&](bool l) { turns.push_back(l); },
                             .leave = [&] { ++stopped; }});
    run(lockpick, UiInput{});
    CHECK(lockpick.focus() == nullptr); // the steps are only shown
    CHECK(lockpick.view().length == 5);
    run(lockpick, step(NavDir::Left));
    run(lockpick, step(NavDir::Right));
    run(lockpick, step(NavDir::Up)); // not a turn
    CHECK(turns == std::vector<bool>{true, false});
    run(lockpick, back());
    CHECK(stopped == 1);
}
