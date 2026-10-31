// The canvas without a GPU (M14 part A): quads, batching by texture and clip, 9-slice geometry, text quads.

#include "TestFont.hpp"

#include <g7/ui/Canvas.hpp>

#include <doctest/doctest.h>

using namespace g7;
using namespace g7::ui;

namespace
{
Canvas makeCanvas()
{
    auto canvas = Canvas::create(nullptr, nullptr);
    REQUIRE(canvas);
    canvas.value().begin(Vec2(800.0f, 600.0f), 1.0f);
    return std::move(canvas).value();
}

// Any non-null address works as a texture identity for batching (never dereferenced without a device).
const render::rhi::Texture* fakeTexture(usize n)
{
    static render::rhi::Texture textures[2];
    return &textures[n];
}
} // namespace

TEST_CASE("Canvas: rectangles batch into one command")
{
    Canvas canvas = makeCanvas();
    canvas.rect(Rect::fromSize(Vec2(10.0f, 20.0f), Vec2(100.0f, 50.0f)), Color{1.0f, 0.0f, 0.0f, 1.0f});
    canvas.rect(Rect::fromSize(Vec2(0.0f), Vec2(5.0f)), Color{});
    canvas.rect(Rect::fromSize(Vec2(0.0f), Vec2(0.0f)), Color{}); // empty: dropped
    CHECK(canvas.vertices().size() == 8);
    CHECK(canvas.indices().size() == 12);
    REQUIRE(canvas.commands().size() == 1);
    CHECK(canvas.commands()[0].texture == nullptr);
    CHECK(canvas.commands()[0].indexCount == 12);
    const Canvas::Vertex& v = canvas.vertices()[2];
    CHECK(v.position == Vec2(110.0f, 70.0f));
    CHECK(v.color == 0xFF0000FFu);
    CHECK(v.pixelRange == 0.0f);
    canvas.end(nullptr);
    CHECK(canvas.vertices().empty());
    CHECK(canvas.commands().empty());
}

TEST_CASE("Canvas: a new texture or clip starts a new command; clipped-away quads vanish")
{
    Canvas canvas = makeCanvas();
    const Rect unit{Vec2(0.0f), Vec2(1.0f)};
    canvas.image(Rect::fromSize(Vec2(0.0f), Vec2(10.0f)), fakeTexture(0), unit, Color{});
    canvas.image(Rect::fromSize(Vec2(20.0f), Vec2(10.0f)), fakeTexture(0), unit, Color{});
    canvas.image(Rect::fromSize(Vec2(40.0f), Vec2(10.0f)), fakeTexture(1), unit, Color{});
    canvas.pushClip(Rect::fromSize(Vec2(0.0f), Vec2(100.0f)));
    canvas.pushClip(Rect::fromSize(Vec2(50.0f), Vec2(100.0f))); // intersected: 50..100
    canvas.image(Rect::fromSize(Vec2(60.0f), Vec2(10.0f)), fakeTexture(1), unit, Color{});
    canvas.image(Rect::fromSize(Vec2(0.0f), Vec2(10.0f)), fakeTexture(1), unit, Color{}); // outside the clip
    canvas.popClip();
    canvas.popClip();
    canvas.image(Rect::fromSize(Vec2(0.0f), Vec2(10.0f)), fakeTexture(1), unit, Color{});
    const auto& commands = canvas.commands();
    REQUIRE(commands.size() == 4);
    CHECK(commands[0].indexCount == 12);
    CHECK(commands[1].texture == fakeTexture(1));
    REQUIRE(commands[2].clip.has_value());
    CHECK(*commands[2].clip == Rect{Vec2(50.0f), Vec2(100.0f)});
    CHECK(commands[2].indexCount == 6);
    CHECK_FALSE(commands[3].clip.has_value());
}

TEST_CASE("Canvas: 9-slice frames")
{
    Canvas canvas = makeCanvas();
    NineSlice slice{fakeTexture(0), Vec2(64.0f, 32.0f), 16.0f, 8.0f, 16.0f, 8.0f, 2.0f};
    canvas.frame(Rect::fromSize(Vec2(100.0f, 100.0f), Vec2(200.0f, 100.0f)), slice, Color{});
    REQUIRE(canvas.vertices().size() == 9 * 4);
    // Top left corner: 32 x 16 pixels (borders x 2), UVs up to the slice line.
    const Canvas::Vertex& corner = canvas.vertices()[2];
    CHECK(corner.position == Vec2(132.0f, 116.0f));
    CHECK(corner.uv.x == doctest::Approx(0.25f));
    CHECK(corner.uv.y == doctest::Approx(0.25f));
    // Bottom right corner ends at the rectangle and UV 1.
    const Canvas::Vertex& last = canvas.vertices()[8 * 4 + 2];
    CHECK(last.position == Vec2(300.0f, 200.0f));
    CHECK(last.uv == Vec2(1.0f, 1.0f));

    // Borders wider than the rectangle shrink proportionally (no overlap, no negative middle).
    canvas.end(nullptr);
    canvas.begin(Vec2(800.0f, 600.0f), 1.0f);
    canvas.frame(Rect::fromSize(Vec2(0.0f), Vec2(32.0f, 100.0f)), slice, Color{});
    CHECK(canvas.vertices()[2].position.x == doctest::Approx(16.0f));

    // Without an image the frame is a plain rectangle.
    canvas.end(nullptr);
    canvas.begin(Vec2(800.0f, 600.0f), 1.0f);
    canvas.frame(Rect::fromSize(Vec2(0.0f), Vec2(10.0f)), NineSlice{}, Color{});
    CHECK(canvas.vertices().size() == 4);
}

TEST_CASE("Canvas: text becomes one quad per visible glyph with the distance range")
{
    Canvas canvas = makeCanvas();
    const Font font = test::makeTestFont();
    const TextLayout layout = layoutText(font, "a b", {.size = 64.0f});
    canvas.text(Vec2(10.0f, 20.0f), layout, {.font = &font, .outline = 2.0f});
    REQUIRE(canvas.vertices().size() == 2 * 4);
    // emPixels 32, range 4: drawn at 64 px per em the field spans 8 screen pixels.
    CHECK(canvas.vertices()[0].pixelRange == doctest::Approx(8.0f));
    CHECK(canvas.vertices()[0].outline == 2.0f);
    CHECK(canvas.vertices()[0].position == Vec2(10.0f, 20.0f) + layout.glyphs[0].min);
    CHECK(font.screenPixelRange(4.0f) == 1.0f); // never below one pixel
    // Without a font nothing is drawn.
    canvas.text(Vec2(0.0f), layout, {});
    CHECK(canvas.vertices().size() == 8);
}
