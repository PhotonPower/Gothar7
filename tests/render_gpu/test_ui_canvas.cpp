// The game UI canvas drawn through the RHI (M14 part A, label "gpu"): colours, clipping, sRGB images and
// distance-field text from the shipped Alegreya atlas (assets/source/fonts, built by g7_fonts).

#include "GlFixture.hpp"

#include <g7/core/FileSystem.hpp>
#include <g7/render/ShaderLibrary.hpp>
#include <g7/render/TextureUpload.hpp>
#include <g7/ui/Canvas.hpp>
#include <g7/ui/Font.hpp>

#include <array>
#include <vector>

using namespace g7;
using namespace g7::render;
using namespace g7::render::rhi;
using g7::test::GlFixture;
using g7::test::require;

namespace
{
constexpr u32 kWidth = 256;
constexpr u32 kHeight = 128;

struct Target
{
    Texture color;
    Framebuffer framebuffer;
};

Target makeTarget(Device& device)
{
    Target t;
    t.color = require(device.createTexture({kWidth, kHeight, Format::RGBA8, 1}));
    t.framebuffer = require(device.createFramebuffer({{&t.color}, nullptr}));
    device.bindFramebuffer(&t.framebuffer);
    device.setViewport(0, 0, kWidth, kHeight);
    device.clear(Vec4(0, 0, 0, 1), std::nullopt);
    return t;
}

/// RGBA at canvas pixel (x, y) with +Y down (readPixels rows are bottom-up).
std::array<u8, 4> at(const std::vector<u8>& pixels, u32 x, u32 y)
{
    const usize i = (static_cast<usize>(kHeight - 1 - y) * kWidth + x) * 4;
    return {pixels[i], pixels[i + 1], pixels[i + 2], pixels[i + 3]};
}
} // namespace

TEST_CASE("UI canvas GPU: rectangles, clipping and sRGB images")
{
    GlFixture gl;
    ShaderLibrary library(*gl.device, fs::fromUtf8(G7_SHADER_DIR));
    ui::Canvas canvas = require(ui::Canvas::create(gl.device.get(), &library));
    Target target = makeTarget(*gl.device);
    // A mid-grey sRGB image must come out as the same display value (decoded by the sampler, encoded again).
    Texture grey = require(createSolidTexture(*gl.device, 128, 128, 128, 255));

    canvas.begin(Vec2(kWidth, kHeight), 1.0f);
    canvas.rect(ui::Rect::fromSize(Vec2(10.0f, 10.0f), Vec2(40.0f, 20.0f)),
                ui::Color{1.0f, 0.0f, 0.0f, 1.0f});
    canvas.pushClip(ui::Rect::fromSize(Vec2(100.0f, 10.0f), Vec2(20.0f, 20.0f)));
    canvas.rect(ui::Rect::fromSize(Vec2(90.0f, 0.0f), Vec2(60.0f, 60.0f)), ui::Color{0.0f, 1.0f, 0.0f, 1.0f});
    canvas.popClip();
    canvas.image(ui::Rect::fromSize(Vec2(10.0f, 60.0f), Vec2(20.0f, 20.0f)), &grey, {Vec2(0.0f), Vec2(1.0f)},
                 ui::Color{});
    canvas.rect(ui::Rect::fromSize(Vec2(200.0f, 60.0f), Vec2(20.0f, 20.0f)),
                ui::Color{1.0f, 1.0f, 1.0f, 0.5f});
    canvas.end(gl.device.get());

    const auto pixels = gl.device->readPixels(0, 0, kWidth, kHeight, &target.framebuffer);
    CHECK(at(pixels, 20, 20)[0] == 255);
    CHECK(at(pixels, 20, 20)[1] == 0);
    CHECK(at(pixels, 9, 20)[0] == 0);     // outside the rectangle
    CHECK(at(pixels, 110, 20)[1] == 255); // inside the clip
    CHECK(at(pixels, 95, 20)[1] == 0);    // clipped away
    CHECK(at(pixels, 110, 40)[1] == 0);
    CHECK(at(pixels, 20, 70)[0] == doctest::Approx(128).epsilon(0.02));
    CHECK(at(pixels, 210, 70)[0] == doctest::Approx(128).epsilon(0.02)); // half-transparent white over black
    CHECK(gl.device->debugErrorCount() == 0);
}

TEST_CASE("UI canvas GPU: distance-field text from the shipped font")
{
    GlFixture gl;
    auto bytes = fs::readFile(fs::fromUtf8(G7_ASSET_SOURCE_DIR) / "fonts" / "alegreya.g7font");
    REQUIRE_MESSAGE(bytes, "assets/source/fonts/alegreya.g7font missing - build the target g7_fonts");
    ui::Font font = require(ui::Font::load(bytes.value(), "alegreya.g7font"));
    const auto uploaded = font.upload(*gl.device);
    REQUIRE_MESSAGE(uploaded.ok(), (uploaded.ok() ? "" : uploaded.error().message));
    ShaderLibrary library(*gl.device, fs::fromUtf8(G7_SHADER_DIR));
    ui::Canvas canvas = require(ui::Canvas::create(gl.device.get(), &library));
    Target target = makeTarget(*gl.device);

    const ui::TextLayout layout = ui::layoutText(font, "IH", {.size = 64.0f});
    REQUIRE(layout.glyphs.size() == 2);
    canvas.begin(Vec2(kWidth, kHeight), 1.0f);
    canvas.text(Vec2(20.0f, 20.0f), layout, {.font = &font, .color = ui::Color{1.0f, 1.0f, 1.0f, 1.0f}});
    canvas.end(gl.device.get());
    const auto pixels = gl.device->readPixels(0, 0, kWidth, kHeight, &target.framebuffer);

    // The middle of the I's stem (the glyph box centre) is solid white; text stays inside its layout box.
    const ui::PlacedGlyph& stem = layout.glyphs[0];
    const Vec2 centre = Vec2(20.0f) + (stem.min + stem.max) * 0.5f;
    CHECK(at(pixels, static_cast<u32>(centre.x), static_cast<u32>(centre.y))[0] >= 250);
    int outside = 0;
    int inside = 0;
    for (u32 y = 0; y < kHeight; ++y)
    {
        for (u32 x = 0; x < kWidth; ++x)
        {
            const bool lit = at(pixels, x, y)[0] > 128;
            const bool inBox = x >= 18 && x <= 22 + static_cast<u32>(layout.size.x) && y >= 18 &&
                               y <= 22 + static_cast<u32>(layout.size.y);
            (inBox ? inside : outside) += lit ? 1 : 0;
        }
    }
    CHECK(inside > 300);
    CHECK(outside == 0);
    // Anti-aliased edges: grey pixels exist (not a hard bitmap).
    int grey = 0;
    for (u32 x = 0; x < kWidth; ++x)
    {
        const u8 v = at(pixels, x, static_cast<u32>(centre.y))[0];
        grey += v > 20 && v < 235 ? 1 : 0;
    }
    CHECK(grey >= 2);
    CHECK(gl.device->debugErrorCount() == 0);
}
