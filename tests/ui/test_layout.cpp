// Layout boxes and layout.toml (M14 part B); the shipped layout and style name what the screens need.

#include <g7/core/FileSystem.hpp>
#include <g7/ui/Layout.hpp>
#include <g7/ui/Style.hpp>

#include <doctest/doctest.h>

#include <ostream>
#include <string>

using namespace g7;
using namespace g7::ui;

TEST_CASE("Layout: anchors place the box on the same point of the parent")
{
    const Rect parent{Vec2(100.0f, 50.0f), Vec2(500.0f, 350.0f)}; // 400 x 300
    const Vec2 size(40.0f, 20.0f);
    const auto at = [&](Anchor anchor, Vec2 offset = Vec2(0.0f), f32 scale = 1.0f)
    { return place({anchor, offset, size}, parent, scale); };
    CHECK(at(Anchor::TopLeft).min == Vec2(100.0f, 50.0f));
    CHECK(at(Anchor::Top).min == Vec2(280.0f, 50.0f));
    CHECK(at(Anchor::TopRight).max == Vec2(500.0f, 70.0f));
    CHECK(at(Anchor::Center).min == Vec2(280.0f, 190.0f));
    CHECK(at(Anchor::BottomLeft).min == Vec2(100.0f, 330.0f));
    CHECK(at(Anchor::BottomRight).max == Vec2(500.0f, 350.0f));
    CHECK(at(Anchor::Right).min == Vec2(460.0f, 190.0f));
    // Offsets and sizes scale; +Y is down, so a negative y offset at the bottom moves up.
    const Rect hud = at(Anchor::BottomLeft, Vec2(10.0f, -10.0f), 2.0f);
    CHECK(hud.min == Vec2(120.0f, 290.0f));
    CHECK(hud.size() == Vec2(80.0f, 40.0f));
    // Size <= 0: the parent's extent plus that value.
    const Rect full = place({Anchor::Top, Vec2(0.0f, 10.0f), Vec2(-100.0f, 0.0f)}, parent, 1.0f);
    CHECK(full.size() == Vec2(300.0f, 300.0f));
    CHECK(full.min == Vec2(150.0f, 60.0f));
}

TEST_CASE("Layout: boxes and numbers from TOML")
{
    CHECK(anchorFromName("bottom_right") == Anchor::BottomRight);
    CHECK_FALSE(anchorFromName("middle").has_value());
    auto layout = Layout::load(R"(
version = 1
[hud.hp]
anchor = "bottom_left"
offset = [32, -32]
size = [260, 22]
[hud.bad]
anchor = "middle"
size = [1, 2, 3]
[inventory]
columns = 5
)");
    REQUIRE(layout);
    const Layout& l = layout.value();
    const LayoutBox hp = l.box("hud.hp");
    CHECK(hp.anchor == Anchor::BottomLeft);
    CHECK(hp.offset == Vec2(32.0f, -32.0f));
    CHECK(hp.size == Vec2(260.0f, 22.0f));
    // Missing or invalid parts keep the fallback.
    const LayoutBox fallback{Anchor::Center, Vec2(1.0f), Vec2(5.0f)};
    const LayoutBox bad = l.box("hud.bad", fallback);
    CHECK(bad.anchor == Anchor::Center);
    CHECK(bad.size == Vec2(5.0f));
    CHECK(l.box("hud.none", fallback).offset == Vec2(1.0f));
    CHECK(l.number("inventory.columns", 0.0f) == 5.0f);
    CHECK(l.number("inventory.rows", 7.0f) == 7.0f);
    CHECK_FALSE(Layout::load("version = 2\n"));
}

TEST_CASE("Layout: the shipped layout and style")
{
    const fs::Path dir = fs::fromUtf8(G7_ASSET_SOURCE_DIR);
    auto layoutText = fs::readText(dir / "ui" / "layout.toml");
    REQUIRE(layoutText);
    auto layout = Layout::load(layoutText.value());
    REQUIRE_MESSAGE(layout, (layout ? "" : layout.error().message));
    for (const char* key : {"hud.hp", "hud.mana", "hud.enemy", "hud.message", "hud.subtitle", "sample.panel"})
    {
        CHECK_MESSAGE(layout.value().contains(key), key);
    }

    auto styleText = fs::readText(dir / "ui" / "style.toml");
    REQUIRE(styleText);
    auto style = loadStyle(styleText.value());
    REQUIRE_MESSAGE(style, (style ? "" : style.error().message));
    // Frame names are a contract with figuren (own frames, #305): they stay, each has an image.
    for (const char* frame :
         {"panel",         "parchment",    "leather",     "tooltip",   "cell",         "cell_selected",
          "cell_equipped", "tab",          "tab_active",  "button",    "button_focus", "button_pressed",
          "bar",           "bar_hp",       "bar_mana",    "bar_enemy", "bar_air",      "subtitle",
          "highlight",     "slider_track", "slider_knob", "bg_wood",   "bg_leather",   "bg_parchment"})
    {
        const FrameStyle* f = style.value().frame(frame);
        REQUIRE_MESSAGE(f != nullptr, frame);
        CHECK_MESSAGE(fs::exists(dir / fs::fromUtf8(f->image)), f->image);
    }
}
