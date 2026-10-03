#pragma once

// Editor gizmos (M4, ADR 0015 addendum: own gizmos, no ImGuizmo): handles for moving, turning and
// scaling, hit in screen pixels so they grab equally well at any distance, and the projection of a
// mouse drag onto an axis, a plane or a ring. Pure maths - drawing is done with Debug-Draw.

#include <g7/core/Math.hpp>

#include <optional>

namespace g7::editor
{
enum class GizmoMode : u8
{
    Translate,
    Rotate,
    Scale,
};

enum class GizmoHandle : u8
{
    None,
    X, ///< axis arrow (translate, scale) or ring around the axis (rotate)
    Y,
    Z,
    PlaneXY, ///< translate in a plane (square between two arms)
    PlaneYZ,
    PlaneXZ,
    Uniform, ///< scale all axes (centre)
};

/// The camera as the gizmo needs it. Mouse and pixel coordinates: origin top left, +Y down.
struct GizmoView
{
    Mat4 viewProjection{1.0f};
    Vec3 eye{0.0f};
    Vec2 viewport{1.0f}; ///< width, height in pixels
};

struct Ray
{
    Vec3 origin{0.0f};
    Vec3 direction{0.0f, 0.0f, -1.0f}; ///< normalised
};

/// Ray from the eye through the pixel `mouse`.
[[nodiscard]] Ray screenRay(const GizmoView& view, const Vec2& mouse) noexcept;
/// Pixel position of a world point; nullopt behind the camera.
[[nodiscard]] std::optional<Vec2> project(const GizmoView& view, const Vec3& point) noexcept;
/// `value` to the nearest multiple of `step` (step <= 0: unchanged).
[[nodiscard]] f32 snap(f32 value, f32 step) noexcept;

class Gizmo
{
public:
    static constexpr f32 kArmPixels = 90.0f; ///< length of an arm on screen
    static constexpr f32 kHitPixels = 8.0f;  ///< grab tolerance
    static constexpr f32 kPlaneFrom = 0.2f;  ///< plane handle square, fraction of an arm
    static constexpr f32 kPlaneTo = 0.4f;
    static constexpr f32 kUniformPixels = 10.0f; ///< half size of the uniform-scale square

    Vec3 origin{0.0f};
    Mat3 axes{1.0f}; ///< columns: x, y, z (world axes or the object's, normalised)
    GizmoMode mode = GizmoMode::Translate;

    /// World length of an arm, so that it spans kArmPixels on screen at the gizmo's distance.
    [[nodiscard]] f32 armLength(const GizmoView& view) const noexcept;
    /// Handle under the mouse (the nearest within kHitPixels), or None.
    [[nodiscard]] GizmoHandle hit(const GizmoView& view, const Vec2& mouse) const noexcept;

    /// Dragging from `start` to `now` (pixels) with `handle`:
    /// translate - world offset along the axis or in the plane;
    [[nodiscard]] Vec3 dragTranslate(GizmoHandle handle, const GizmoView& view, const Vec2& start,
                                     const Vec2& now) const noexcept;
    /// rotate - signed angle in radians about the handle's axis (right-handed);
    [[nodiscard]] f32 dragRotate(GizmoHandle handle, const GizmoView& view, const Vec2& start,
                                 const Vec2& now) const noexcept;
    /// scale - factor per axis (1 = unchanged; Uniform scales all by the vertical mouse movement).
    [[nodiscard]] Vec3 dragScale(GizmoHandle handle, const GizmoView& view, const Vec2& start,
                                 const Vec2& now) const noexcept;

    /// Axis of an axis or ring handle (X, Y, Z); zero for others.
    [[nodiscard]] Vec3 axisOf(GizmoHandle handle) const noexcept;
};
} // namespace g7::editor
