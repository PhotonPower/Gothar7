// The item screens without a GPU (M14 part D): tabs, using and dropping, quantity choice when trading,
// containers, closing; the shipped tabs cover every item category once.

#include <g7/core/FileSystem.hpp>
#include <g7/ui/ItemScreen.hpp>
#include <g7/ui/Layout.hpp>

#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <ostream>
#include <string>
#include <vector>

using namespace g7;
using namespace g7::ui;

namespace
{
const Rect kScreen{Vec2(0.0f), Vec2(1920.0f, 1080.0f)};

/// The item categories of the Lua items (gameplay/Character.hpp kItemCategories).
constexpr std::array<std::string_view, 18> kCategories = {
    "melee_1h", "melee_2h", "bow",    "crossbow", "ammo", "armor",    "helmet", "ring",  "amulet",
    "belt",     "rune",     "scroll", "potion",   "food", "document", "key",    "torch", "misc"};

UiInput with(void (*set)(UiInput&))
{
    UiInput in;
    set(in);
    return in;
}

void run(Screen& screen, const UiInput& in = {})
{
    screen.update(&in, 0.016f, kScreen, 1.0f, nullptr);
}

ItemEntry entry(std::string item, std::string category, u32 count = 1, i64 price = 10)
{
    ItemEntry e;
    e.item = item;
    e.name = item;
    e.category = std::move(category);
    e.count = count;
    e.price = price;
    return e;
}

const std::vector<ItemTab> kTabs = {{"W", {"melee_1h"}}, {"F", {"food"}}, {"M", {"misc"}}};
} // namespace

TEST_CASE("Item screens: the shipped tabs cover every category once")
{
    auto text = fs::readText(fs::fromUtf8(G7_ASSET_SOURCE_DIR) / "ui" / "layout.toml");
    REQUIRE(text);
    auto layout = Layout::load(text.value());
    REQUIRE(layout);
    const std::vector<ItemTab> tabs = itemTabs(layout.value());
    CHECK(tabs.size() == 7);
    for (const std::string_view category : kCategories)
    {
        usize found = 0;
        for (const ItemTab& tab : tabs)
        {
            found += static_cast<usize>(std::count(tab.categories.begin(), tab.categories.end(), category));
        }
        CHECK_MESSAGE(found == 1, category);
    }
}

TEST_CASE("Item screens: the inventory uses, drops and switches tabs")
{
    ItemScreenView view;
    view.hero.items = {entry("it_apple", "food", 5), entry("it_bread", "food"), entry("it_jug", "misc")};
    std::vector<std::string> used;
    std::vector<std::string> dropped;
    int closed = 0;
    ItemScreen screen(ItemScreenMode::Inventory,
                      {.view = [&] { return view; },
                       .use = [&](const ItemEntry& e) { used.push_back(e.item); },
                       .drop = [&](const ItemEntry& e) { dropped.push_back(e.item); },
                       .close = [&] { ++closed; }},
                      kTabs);
    CHECK(screen.name() == "inventory");
    CHECK_FALSE(screen.flags().pausesGame); // F4: the world goes on
    run(screen);
    CHECK(screen.tab() == 1); // the first tab with something in it (no weapons)
    REQUIRE(screen.heroItems().size() == 2);
    CHECK(screen.focus() == &screen.heroGrid());
    run(screen, with([](UiInput& in) { in.nav = NavDir::Right; }));
    REQUIRE(screen.selected() != nullptr);
    CHECK(screen.selected()->item == "it_bread");
    run(screen, with([](UiInput& in) { in.accept = true; }));
    CHECK(used == std::vector<std::string>{"it_bread"});
    run(screen, with([](UiInput& in) { in.drop = true; }));
    CHECK(dropped == std::vector<std::string>{"it_bread"});
    run(screen, with([](UiInput& in) { in.nextTab = true; }));
    CHECK(screen.tab() == 2);
    REQUIRE(screen.heroItems().size() == 1);
    CHECK(screen.heroItems().front().item == "it_jug");
    run(screen, with([](UiInput& in) { in.nextTab = true; })); // wraps around
    CHECK(screen.tab() == 0);
    CHECK(screen.heroItems().empty());
    run(screen, with([](UiInput& in) { in.back = true; }));
    CHECK(closed == 1);
    CHECK(screen.closed());
}

TEST_CASE("Item screens: trading asks how many of a stack (F5)")
{
    ItemScreenView view;
    view.hero.items = {entry("it_apple", "food", 6, 2)};
    view.other.items = {entry("it_sword", "melee_1h", 1, 50), entry("it_bread", "food", 9, 3)};
    struct Transfer
    {
        bool fromOther;
        std::string item;
        u32 count;
    };
    std::vector<Transfer> transfers;
    ItemScreen screen(
        ItemScreenMode::Trade,
        {.view = [&] { return view; },
         .transfer = [&](bool o, const ItemEntry& e, u32 n) { transfers.push_back({o, e.item, n}); },
         .maxTransfer = [](bool, const ItemEntry& e) { return std::min<u32>(e.count, 7); }},
        kTabs);
    CHECK(screen.name() == "trade");
    run(screen);
    CHECK(screen.tab() == 0); // "all" first
    CHECK(screen.tabs().front().key == "UI_TAB_ALL");
    CHECK(screen.focus() == &screen.otherGrid()); // the trader's goods first
    // A single item: bought at once.
    run(screen, with([](UiInput& in) { in.accept = true; }));
    REQUIRE(transfers.size() == 1);
    CHECK(transfers[0].fromOther);
    CHECK(transfers[0].count == 1);
    // A stack: the quantity choice, starting at one, limited by maxTransfer (the money).
    run(screen, with([](UiInput& in) { in.nav = NavDir::Right; }));
    CHECK(screen.selected()->item == "it_bread");
    run(screen, with([](UiInput& in) { in.accept = true; }));
    REQUIRE(screen.quantityOpen());
    CHECK(screen.quantity() == 1);
    run(screen, with([](UiInput& in) { in.nav = NavDir::Right; }));
    run(screen, with([](UiInput& in) { in.nextTab = true; })); // +10, capped at 7
    CHECK(screen.quantity() == 7);
    run(screen, with([](UiInput& in) { in.nav = NavDir::Left; }));
    CHECK(screen.quantity() == 6);
    CHECK(screen.tab() == 0); // the tab keys changed the amount, not the tab
    run(screen, with([](UiInput& in) { in.accept = true; }));
    CHECK_FALSE(screen.quantityOpen());
    REQUIRE(transfers.size() == 2);
    CHECK(transfers[1].item == "it_bread");
    CHECK(transfers[1].count == 6);
    // Back in the quantity choice cancels it, not the screen.
    run(screen, with([](UiInput& in) { in.accept = true; }));
    REQUIRE(screen.quantityOpen());
    run(screen, with([](UiInput& in) { in.back = true; }));
    CHECK_FALSE(screen.quantityOpen());
    CHECK_FALSE(screen.closed());
    CHECK(transfers.size() == 2);
    // Over to the hero's side: selling.
    run(screen, with([](UiInput& in) { in.nav = NavDir::Right; })); // grid edge -> the hero's grid
    CHECK(screen.focus() == &screen.heroGrid());
    CHECK_FALSE(screen.selectedInOther());
}

TEST_CASE("Item screens: a container offers all of a stack")
{
    ItemScreenView view;
    view.other.items = {entry("it_apple", "food", 4)};
    std::vector<u32> counts;
    ItemScreen screen(ItemScreenMode::Container,
                      {.view = [&] { return view; },
                       .transfer = [&](bool, const ItemEntry&, u32 n) { counts.push_back(n); }},
                      kTabs);
    run(screen);
    run(screen, with([](UiInput& in) { in.accept = true; }));
    REQUIRE(screen.quantityOpen());
    CHECK(screen.quantity() == 4);
    run(screen, with([](UiInput& in) { in.accept = true; }));
    CHECK(counts == std::vector<u32>{4});
}
