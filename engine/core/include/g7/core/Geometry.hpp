#pragma once

#include <g7/core/Math.hpp>

#include <array>

namespace g7
{
/// Plane n·p + d = 0 with unit normal; positive distance = in front (the side the normal points to).
struct Plane
{
    Vec3 normal{0.0f, 1.0f, 0.0f};
    f32 d = 0.0f;

    [[nodiscard]] f32 distance(const Vec3& point) const noexcept { return glm::dot(normal, point) + d; }
    [[nodiscard]] static Plane fromPointNormal(const Vec3& point, const Vec3& normal) noexcept;
    /// From (a, b, c, d) coefficients; normalises them.
    [[nodiscard]] static Plane fromCoefficients(const Vec4& coefficients) noexcept;
};

struct Sphere
{
    Vec3 center{0.0f};
    f32 radius = 0.0f;
};

struct AABB
{
    Vec3 min{0.0f};
    Vec3 max{0.0f};

    [[nodiscard]] Vec3 center() const noexcept { return (min + max) * 0.5f; }
    [[nodiscard]] Vec3 extents() const noexcept { return (max - min) * 0.5f; }
    /// Box enclosing this box after an affine transform (stays axis-aligned, may grow).
    [[nodiscard]] AABB transformed(const Mat4& matrix) const noexcept;
};

/// View frustum as six inward-facing planes, extracted from a view-projection matrix with a
/// 0..1 depth range (works for reverse-Z too). Tests are conservative: they may report an
/// intersection for objects just outside near a corner, never miss a visible object.
class Frustum
{
public:
    enum Side : u8
    {
        Left,
        Right,
        Bottom,
        Top,
        DepthZero, ///< far plane with reverse-Z
        DepthOne,  ///< near plane with reverse-Z
        Count
    };

    [[nodiscard]] static Frustum fromViewProjection(const Mat4& viewProjection) noexcept;

    [[nodiscard]] bool contains(const Vec3& point) const noexcept;
    [[nodiscard]] bool intersects(const Sphere& sphere) const noexcept;
    [[nodiscard]] bool intersects(const AABB& box) const noexcept;
    [[nodiscard]] const Plane& plane(Side side) const noexcept { return m_planes[side]; }

private:
    std::array<Plane, Count> m_planes;
};

/// Right-handed perspective projection (camera looks along -Z) with **reverse-Z** and a 0..1 depth
/// range (ADR 0002): depth 1 at the near plane, 0 at the far plane. Requires the GL clip control
/// set by render::Device; depth tests use GreaterEqual and depth is cleared to 0.
[[nodiscard]] Mat4 perspectiveReverseZ(f32 fovY, f32 aspect, f32 nearPlane, f32 farPlane) noexcept;
} // namespace g7
