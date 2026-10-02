#pragma once

#include <g7/core/Math.hpp>

namespace g7
{
/// Position, rotation and scale; the matrix form is T * R * S.
///
/// Composition is exact for uniform scale. Non-uniform scale combined with rotation would
/// need shear, which a Transform cannot represent; such hierarchies use matrices instead.
struct Transform
{
    Vec3 position{0.0f};
    Quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
    Vec3 scale{1.0f};

    [[nodiscard]] Mat4 toMatrix() const noexcept;
    /// Decomposes an affine matrix without shear or projection. A negative determinant
    /// (mirroring) is expressed as negative x scale.
    [[nodiscard]] static Transform fromMatrix(const Mat4& matrix) noexcept;

    /// parent * local -> world, same as multiplying the matrices.
    [[nodiscard]] Transform operator*(const Transform& child) const noexcept;
    [[nodiscard]] Transform inverse() const noexcept;

    [[nodiscard]] Vec3 transformPoint(const Vec3& point) const noexcept;
    /// Rotates a direction; ignores translation and scale.
    [[nodiscard]] Vec3 transformDirection(const Vec3& direction) const noexcept;

    [[nodiscard]] Vec3 forward() const noexcept;
    [[nodiscard]] Vec3 right() const noexcept;
    [[nodiscard]] Vec3 up() const noexcept;
};

/// Blends two transforms (lerp position/scale, shortest-path slerp rotation). Used to render
/// between two fixed simulation steps.
[[nodiscard]] Transform interpolate(const Transform& a, const Transform& b, f32 t) noexcept;
} // namespace g7
