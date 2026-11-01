// Inventory cells (M14 part D): inventory_views.toml v2 and the cell geometry of the contract, checked
// against figuren's reference implementation (tools/chargen inventory.py `cell()`, values computed with it).

#include <g7/core/FileSystem.hpp>
#include <g7/ui/ItemCell.hpp>

#include <doctest/doctest.h>

#include <cmath>
#include <ostream>
#include <vector>

using namespace g7;
using namespace g7::ui;

namespace
{
std::vector<Vec3> boxCorners(Vec3 min, Vec3 max)
{
    std::vector<Vec3> out;
    for (int i = 0; i < 8; ++i)
    {
        out.emplace_back((i & 4) ? max.x : min.x, (i & 2) ? max.y : min.y, (i & 1) ? max.z : min.z);
    }
    return out;
}

/// A sword like the test of inventory.py: blade, cross-guard, grip (all boxes).
std::vector<Vec3> sword()
{
    std::vector<Vec3> v = boxCorners(Vec3(-0.02f, 0.0f, -0.004f), Vec3(0.02f, 0.9f, 0.004f));
    const auto guard = boxCorners(Vec3(-0.1f, -0.015f, -0.015f), Vec3(0.1f, 0.015f, 0.015f));
    const auto grip = boxCorners(Vec3(-0.015f, -0.2f, -0.015f), Vec3(0.015f, 0.0f, 0.015f));
    v.insert(v.end(), guard.begin(), guard.end());
    v.insert(v.end(), grip.begin(), grip.end());
    return v;
}
} // namespace

TEST_CASE("Item cells: the contract's geometry matches the reference implementation")
{
    // A box 1 x 2 x 0.5 off the origin, default view (yaw 25, pitch 15, fill 0.75).
    const auto box = boxCorners(Vec3(-0.3f, -0.9f, -0.55f), Vec3(0.7f, 1.1f, -0.05f));
    auto cell = computeCell(box, {25.0f, 15.0f, 0.0f, 0.75f}, 30.0f);
    REQUIRE(cell);
    const CellFrame& f = cell.value();
    CHECK(f.centre.x == doctest::Approx(0.05447608).epsilon(1e-4));
    CHECK(f.centre.y == doctest::Approx(0.18883983).epsilon(1e-4));
    CHECK(f.centre.z == doctest::Approx(-0.31838950).epsilon(1e-4));
    CHECK(f.distance == doctest::Approx(4.19199616).epsilon(1e-4));
    CHECK(std::abs(f.window.x - 0.00198816f) < 1e-5f);
    CHECK(std::abs(f.window.y + 0.03768160f) < 1e-5f);
    CHECK(f.halfSize == doctest::Approx(1.28309120).epsilon(1e-4));
    // The contour fills 0.75 of the cell in its larger (vertical) direction and is centred.
    Vec2 lo(1e9f);
    Vec2 hi(-1e9f);
    for (const Vec3& v : box)
    {
        const Vec2 u = projectInCell(f, v);
        lo = glm::min(lo, u);
        hi = glm::max(hi, u);
    }
    CHECK(hi.y == doctest::Approx(0.75f).epsilon(1e-4));
    CHECK(lo.y == doctest::Approx(-0.75f).epsilon(1e-4));
    CHECK(hi.x == doctest::Approx(-lo.x).epsilon(1e-4));

    // A sword diagonal (category melee_1h: yaw 90, roll -45, fill 0.97).
    auto blade = computeCell(sword(), {90.0f, 0.0f, -45.0f, 0.97f}, 30.0f);
    REQUIRE(blade);
    CHECK(blade.value().distance == doctest::Approx(1.49649724).epsilon(1e-4));
    CHECK(blade.value().halfSize == doctest::Approx(1.02918816).epsilon(1e-4));
    CHECK(std::abs(blade.value().window.x - 0.00168748f) < 1e-5f);
}

TEST_CASE("Item cells: turning keeps every vertex in front of the camera")
{
    const auto v = sword();
    auto cell = computeCell(v, {90.0f, 0.0f, -45.0f, 0.97f}, 30.0f);
    REQUIRE(cell);
    for (int step = 0; step < 16; ++step)
    {
        const Mat4 m = cell.value().modelMatrix(static_cast<f32>(step) * 0.4f);
        for (const Vec3& p : v)
        {
            CHECK((m * Vec4(p, 1.0f)).z < cell.value().distance);
        }
    }
    CHECK_FALSE(computeCell({}, {}, 30.0f));
    const std::vector<Vec3> point(3, Vec3(1.0f));
    CHECK_FALSE(computeCell(point, {}, 30.0f)); // no extent
}

TEST_CASE("Item cells: inventory_views.toml v2")
{
    auto views = InventoryViews::load(R"(
version = 2
[camera]
fov = 40.0
[default]
yaw = 25.0
pitch = 15.0
roll = 0.0
fill = 0.75
[category.melee_1h]
yaw = 90.0
roll = -45.0
fill = 0.97
[model."items/it_mug.glb"]
yaw = -70.0
[model."items/it_lute.glb"]
category = "melee_1h"
pitch = 5.0
)");
    REQUIRE_MESSAGE(views, (views ? "" : views.error().message));
    const InventoryViews& v = views.value();
    CHECK(v.fovDegrees() == 40.0f);
    const InventoryView sword = v.resolve("items/it_sword.glb", "melee_1h");
    CHECK(sword.yaw == 90.0f);
    CHECK(sword.pitch == 15.0f); // from the default
    CHECK(sword.fill == 0.97f);
    CHECK(v.resolve("items/it_mug.glb", "misc").yaw == -70.0f);
    const InventoryView lute = v.resolve("items/it_lute.glb", "misc"); // its own category wins
    CHECK(lute.roll == -45.0f);
    CHECK(lute.pitch == 5.0f);
    CHECK(v.resolve("", "food").fill == 0.75f); // no mesh: the category view

    CHECK_FALSE(InventoryViews::load("version = 1\n[default]\nyaw = 0\npitch = 0\nroll = 0\nfill = 0.5\n"));
    CHECK_FALSE(InventoryViews::load("version = 2\n[default]\nyaw = 0\n"));
    CHECK_FALSE(InventoryViews::load(
        "version = 2\n[default]\nyaw = 0\npitch = 0\nroll = 0\nfill = 0.5\n[category.ring]\nfill = 2\n"));

    // The shipped file.
    auto text = fs::readText(fs::fromUtf8(G7_ASSET_SOURCE_DIR) / "data" / "inventory_views.toml");
    REQUIRE(text);
    auto shipped = InventoryViews::load(text.value());
    REQUIRE_MESSAGE(shipped, (shipped ? "" : shipped.error().message));
    CHECK(shipped.value().resolve("", "melee_1h").roll == doctest::Approx(-45.0f));
}
