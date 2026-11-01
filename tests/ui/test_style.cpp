// style.toml (M14 part A): colours, text roles, frames, sounds; errors name the entry.

#include <g7/ui/Style.hpp>

#include <doctest/doctest.h>

#include <ostream>
#include <string>

using namespace g7;
using namespace g7::ui;

TEST_CASE("Style: colours")
{
    auto opaque = parseColor("#ff8000");
    REQUIRE(opaque);
    CHECK(opaque.value() == Color{1.0f, 128.0f / 255.0f, 0.0f, 1.0f});
    auto translucent = parseColor("#FFFFFF80");
    REQUIRE(translucent);
    CHECK(translucent.value().a == doctest::Approx(128.0f / 255.0f));
    CHECK_FALSE(parseColor("ff8000"));
    CHECK_FALSE(parseColor("#ff80"));
    CHECK_FALSE(parseColor("#gg8000"));
    CHECK(Color{1.0f, 0.0f, 0.0f, 1.0f}.packed() == 0xFF0000FFu);
    CHECK(Color{0.0f, 0.0f, 1.0f, 0.5f}.packed() == 0x80FF0000u);
}

TEST_CASE("Style: a complete file")
{
    const char* toml = R"(
version = 1
[colors]
text = "#e8dcc0"
highlight = "#ffd36b"
[fonts]
body = { font = "fonts/alegreya.g7font", size = 22, color = "text" }
title = { font = "fonts/alegreya_sc.g7font", size = 34, color = "#ffffff", outline = 1.5, outline_color = "highlight" }
[frames.panel]
image = "ui/frames/panel.png"
slice = [24, 20, 24, 28]
scale = 0.5
[sounds]
select = "ui_select"
)";
    auto style = loadStyle(toml, "style.toml");
    REQUIRE_MESSAGE(style, (style ? "" : style.error().message));
    const Style& s = style.value();
    CHECK(s.color("text") == parseColor("#e8dcc0").value());
    const TextRole& body = s.font("body");
    CHECK(body.font == "fonts/alegreya.g7font");
    CHECK(body.size == 22.0f);
    CHECK(body.color == s.color("text"));
    CHECK(body.outline == 0.0f);
    const TextRole& title = s.font("title");
    CHECK(title.outline == 1.5f);
    CHECK(title.outlineColor == s.color("highlight"));
    const FrameStyle* panel = s.frame("panel");
    REQUIRE(panel != nullptr);
    CHECK(panel->image == "ui/frames/panel.png");
    CHECK(panel->top == 20.0f);
    CHECK(panel->bottom == 28.0f);
    CHECK(panel->scale == 0.5f);
    CHECK(s.sound("select") == "ui_select");
    CHECK(s.sound("nothing").empty());
    // Unknown names: a warning and a visible fallback.
    CHECK(s.color("nope") == Color{1.0f, 0.0f, 1.0f, 1.0f});
    CHECK(s.frame("nope") == nullptr);
    CHECK_FALSE(s.font("nope").font.empty());
}

TEST_CASE("Style: errors name the entry")
{
    CHECK_FALSE(loadStyle("version = 2"));
    auto badColor = loadStyle("version = 1\n[colors]\ntext = \"red\"\n");
    REQUIRE_FALSE(badColor);
    CHECK(badColor.error().message.find("colors.text") != std::string::npos);
    auto unknownRef = loadStyle("version = 1\n[fonts]\nbody = { font = \"f.g7font\", color = \"nope\" }\n");
    REQUIRE_FALSE(unknownRef);
    CHECK(unknownRef.error().message.find("fonts.body") != std::string::npos);
    CHECK_FALSE(loadStyle("version = 1\n[fonts]\nbody = { size = 20 }\n"));
    CHECK_FALSE(loadStyle("version = 1\n[frames.panel]\nimage = \"a.png\"\nslice = [1, 2, 3]\n"));
    // Unknown sections only warn.
    CHECK(loadStyle("version = 1\n[item_preview]\nfoo = 1\n"));
}
