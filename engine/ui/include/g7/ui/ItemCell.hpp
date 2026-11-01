#pragma once

// Inventory cells (M14 part D): which way an item model faces in its cell and where the camera looks -
// assets/source/data/inventory_views.toml v2, contract figuren -> engine (docs/modules/ui.md
// "Inventar-Ansichten"; reference implementation tools/chargen inventory.py `cell()`).

#include <g7/core/Math.hpp>
#include <g7/core/Result.hpp>
#include <g7/core/Types.hpp>

#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace g7::ui
{
/// yaw/pitch/roll in degrees, fill = share of the cell the projected contour takes in its larger direction.
struct InventoryView
{
    f32 yaw = 25.0f;
    f32 pitch = 15.0f;
    f32 roll = 0.0f;
    f32 fill = 0.75f;
};

/// inventory_views.toml v2: [camera] fov, [default] (all four fields), [category.<c>] and [model."<mesh>"]
/// (fields optional; a model may name its own `category`).
class InventoryViews
{
public:
    [[nodiscard]] static Result<InventoryViews>
    load(std::string_view toml, std::string_view sourceName = "data/inventory_views.toml");
    /// default <- category <- model, field by field; the category is the model's own or `itemCategory`.
    [[nodiscard]] InventoryView resolve(std::string_view mesh, std::string_view itemCategory) const;
    [[nodiscard]] f32 fovDegrees() const noexcept { return m_fov; }

private:
    struct Partial
    {
        std::optional<f32> yaw, pitch, roll, fill;
        std::string category;
    };
    static void apply(InventoryView& view, const Partial& partial);
    f32 m_fov = 30.0f;
    InventoryView m_default;
    std::map<std::string, Partial, std::less<>> m_categories;
    std::map<std::string, Partial, std::less<>> m_models;
};

/// Where a model sits in its cell: model matrix = translate(-centre) * rotation; the camera on +Z at
/// `distance` looking down -Z with the full `fov`, of which the cell shows the window (`window` centre,
/// `halfSize`, in units of the full view: an off-axis projection).
struct CellFrame
{
    Mat3 rotation{1.0f};
    Vec3 centre{0.0f};
    f32 distance = 1.0f;
    f32 fovDegrees = 30.0f;
    Vec2 window{0.0f};
    f32 halfSize = 1.0f;

    /// Model to view space (camera at the origin of the view: z shifted by -distance).
    [[nodiscard]] Mat4 modelMatrix(f32 spinRadians = 0.0f) const noexcept;
};

/// The cell geometry of the contract (steps 1-4). Fails for empty or degenerate vertex sets.
[[nodiscard]] Result<CellFrame> computeCell(std::span<const Vec3> lod0Positions, const InventoryView& view,
                                            f32 fovDegrees);

/// A vertex's position in the cell (-1..1 = the whole cell, +Y up); tests and tools.
[[nodiscard]] Vec2 projectInCell(const CellFrame& frame, const Vec3& vertex) noexcept;
} // namespace g7::ui
