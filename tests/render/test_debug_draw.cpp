#include <g7/render/Camera.hpp>
#include <g7/render/DebugDraw.hpp>

#include <doctest/doctest.h>
#include <glm/gtc/matrix_transform.hpp>

#include <cmath>

using namespace g7;
using namespace g7::render;

TEST_CASE("DebugDraw: shapes produce the expected lines")
{
    DebugDraw debug;
    debug.box(AABB{Vec3(-1, 0, -2), Vec3(1, 3, 2)});
    REQUIRE(debug.lineCount() == 12);
    for (const auto& l : debug.lines())
    {
        // Every edge is axis-aligned and runs between two corners of the box.
        const Vec3 d = glm::abs(l.b - l.a);
        CHECK(int(d.x > 0) + int(d.y > 0) + int(d.z > 0) == 1);
        for (const Vec3& p : {l.a, l.b})
        {
            CHECK((p.x == -1 || p.x == 1));
            CHECK((p.y == 0 || p.y == 3));
            CHECK((p.z == -2 || p.z == 2));
        }
    }

    debug.clear();
    const Vec3 centre(2, 1, -3);
    debug.sphere(centre, 2.5f);
    CHECK(debug.lineCount() == 3 * DebugDraw::kCircleSegments);
    for (const auto& l : debug.lines())
    {
        CHECK(glm::length(l.a - centre) == doctest::Approx(2.5f));
        CHECK(glm::length(l.b - centre) == doctest::Approx(2.5f));
    }

    debug.clear();
    debug.circle(Vec3(0), Vec3(0, 0, 1), 1.0f);
    for (const auto& l : debug.lines())
    {
        CHECK(l.a.z == doctest::Approx(0.0f)); // in the plane of its normal
    }

    debug.clear();
    debug.arrow(Vec3(0), Vec3(0, 0, 5));
    CHECK(debug.lineCount() == 5);
    debug.clear();
    debug.cross(Vec3(1), 2.0f);
    debug.axes(Mat4(1.0f), 1.0f);
    CHECK(debug.lineCount() == 6);
    CHECK(debug.lines()[3].color == DebugDraw::packColor(Vec4(1, 0.2f, 0.2f, 1))); // X is red
    debug.clear();
    debug.grid(Vec3(0), 10.0f, 1.0f);
    CHECK(debug.lineCount() == 2 * 11);
    debug.clear();
    debug.grid(Vec3(0), 10.0f, 0.0f); // invalid spacing: nothing
    CHECK(debug.lineCount() == 0);
}

TEST_CASE("DebugDraw: oriented box and frustum corners")
{
    DebugDraw debug;
    const Mat4 transform =
        glm::translate(Mat4(1.0f), Vec3(5, 0, 0)) * glm::rotate(Mat4(1.0f), toRadians(90.0f), Vec3(0, 1, 0));
    debug.box(transform, Vec3(2, 1, 0.5f));
    REQUIRE(debug.lineCount() == 12);
    for (const auto& l : debug.lines())
    {
        // Rotated 90° about Y: the 2 m half extent now lies along Z.
        CHECK(std::abs(l.a.z) == doctest::Approx(2.0f));
        CHECK(std::abs(l.a.x - 5.0f) == doctest::Approx(0.5f));
    }

    debug.clear();
    Camera camera;
    camera.aspect = 1.0f;
    camera.nearPlane = 1.0f;
    camera.farPlane = 10.0f;
    camera.fovY = toRadians(90.0f);
    debug.frustum(camera.viewProjection());
    REQUIRE(debug.lineCount() == 12);
    // The camera looks along -Z: corners at z = -1 (half size 1) and z = -10 (half size 10).
    for (const auto& l : debug.lines())
    {
        for (const Vec3& p : {l.a, l.b})
        {
            const bool nearCorner = std::abs(p.z + 1.0f) < 1e-3f;
            const bool farCorner = std::abs(p.z + 10.0f) < 1e-2f;
            CHECK((nearCorner || farCorner));
            CHECK(std::abs(p.x) == doctest::Approx(nearCorner ? 1.0f : 10.0f).epsilon(1e-3));
        }
    }
}

TEST_CASE("DebugDraw: lifetime")
{
    DebugDraw debug;
    debug.line(Vec3(0), Vec3(1));                  // this frame only
    debug.line(Vec3(0), Vec3(1), {Vec4(1), 1.0f}); // one second
    debug.text(Vec3(0), "x", {Vec4(1), 0.0f});
    CHECK(debug.lineCount() == 2);
    debug.advance(0.0f); // single-frame items go even without time passing
    CHECK(debug.lineCount() == 1);
    CHECK(debug.texts().empty());
    debug.advance(0.5f);
    CHECK(debug.lineCount() == 1);
    debug.advance(0.6f);
    CHECK(debug.lineCount() == 0);
}

TEST_CASE("DebugDraw: glyphs and colours")
{
    CHECK(DebugDraw::glyphs("A b") == std::vector<i32>{'A' - 32, 0, 'b' - 32});
    // "ä" (2 bytes) and "€" (3 bytes) become one '?' each; '\n' is -1; tab is dropped.
    CHECK(DebugDraw::glyphs("\xC3\xA4\xE2\x82\xAC\n\t~") ==
          std::vector<i32>{'?' - 32, '?' - 32, -1, '~' - 32});
    CHECK(DebugDraw::packColor(Vec4(1, 0, 0, 1)) == 0xFF0000FFu);
    CHECK(DebugDraw::packColor(Vec4(2, -1, 0.5f, 0)) == 0x008000FFu);
}

TEST_CASE("DebugDraw: text layout")
{
    Camera camera;
    camera.aspect = 1.0f;
    const Mat4 viewProjection = camera.viewProjection();
    std::vector<DebugGlyphQuad> quads;

    // Centred on the projected point: straight ahead = the middle of a 100x100 viewport.
    DebugDraw::Text text{"AB", Vec3(0, 0, -5), 0xFFFFFFFFu, 1.0f, 0.0f, true, false};
    layoutDebugText(text, viewProjection, 100, 100, quads);
    REQUIRE(quads.size() == 4); // shadow + glyph for each letter
    const DebugGlyphQuad& a = quads[2];
    CHECK(a.min == Vec2(42, 46)); // 16 px wide, 8 px high around (50, 50)
    CHECK(a.max == Vec2(50, 54));
    CHECK(a.glyph == 'A' - 32);
    CHECK(a.depth > 0.0f);
    CHECK(quads[0].min == a.min + Vec2(1)); // shadow one pixel down-right
    CHECK((quads[0].color >> 24) < 255u);

    // Behind the camera: nothing.
    quads.clear();
    text.position = Vec3(0, 0, 5);
    layoutDebugText(text, viewProjection, 100, 100, quads);
    CHECK(quads.empty());

    // Screen text at a pixel position, scaled, multi-line, never depth-tested; spaces emit nothing.
    quads.clear();
    DebugDraw::Text screen{"A\n B", Vec3(10, 20, 0), 0xFF00FF00u, 2.0f, 0.0f, false, true};
    layoutDebugText(screen, viewProjection, 100, 100, quads);
    REQUIRE(quads.size() == 4);
    CHECK(quads[2].min == Vec2(10, 20));
    CHECK(quads[2].max == Vec2(26, 36));
    CHECK(quads[3].min == Vec2(26, 36)); // second line, after the space
    CHECK(quads[3].depth < 0.0f);
}
