#include <g7/core/Geometry.hpp>

#include <cmath>

namespace g7
{
Plane Plane::fromPointNormal(const Vec3& point, const Vec3& normal) noexcept
{
    const Vec3 n = glm::normalize(normal);
    return Plane{n, -glm::dot(n, point)};
}

Plane Plane::fromCoefficients(const Vec4& coefficients) noexcept
{
    const Vec3 n(coefficients);
    const f32 length = glm::length(n);
    if (length <= 0.0f)
    {
        return Plane{};
    }
    return Plane{n / length, coefficients.w / length};
}

AABB AABB::transformed(const Mat4& matrix) const noexcept
{
    // Arvo: transform the centre, project the extents onto the absolute rotation/scale part.
    const Vec3 c = Vec3(matrix * Vec4(center(), 1.0f));
    const Vec3 e = extents();
    const Mat3 m(matrix);
    const Vec3 newExtents = Vec3(std::abs(m[0][0]) * e.x + std::abs(m[1][0]) * e.y + std::abs(m[2][0]) * e.z,
                                 std::abs(m[0][1]) * e.x + std::abs(m[1][1]) * e.y + std::abs(m[2][1]) * e.z,
                                 std::abs(m[0][2]) * e.x + std::abs(m[1][2]) * e.y + std::abs(m[2][2]) * e.z);
    return AABB{c - newExtents, c + newExtents};
}

Frustum Frustum::fromViewProjection(const Mat4& m) noexcept
{
    // Gribb/Hartmann for clip space -w <= x,y <= w and 0 <= z <= w.
    const auto row = [&](int i) { return Vec4(m[0][i], m[1][i], m[2][i], m[3][i]); };
    const Vec4 r0 = row(0);
    const Vec4 r1 = row(1);
    const Vec4 r2 = row(2);
    const Vec4 r3 = row(3);
    Frustum frustum;
    frustum.m_planes[Left] = Plane::fromCoefficients(r3 + r0);
    frustum.m_planes[Right] = Plane::fromCoefficients(r3 - r0);
    frustum.m_planes[Bottom] = Plane::fromCoefficients(r3 + r1);
    frustum.m_planes[Top] = Plane::fromCoefficients(r3 - r1);
    frustum.m_planes[DepthZero] = Plane::fromCoefficients(r2);
    frustum.m_planes[DepthOne] = Plane::fromCoefficients(r3 - r2);
    return frustum;
}

bool Frustum::contains(const Vec3& point) const noexcept
{
    for (const Plane& plane : m_planes)
    {
        if (plane.distance(point) < 0.0f)
        {
            return false;
        }
    }
    return true;
}

bool Frustum::intersects(const Sphere& sphere) const noexcept
{
    for (const Plane& plane : m_planes)
    {
        if (plane.distance(sphere.center) < -sphere.radius)
        {
            return false;
        }
    }
    return true;
}

bool Frustum::intersects(const AABB& box) const noexcept
{
    for (const Plane& plane : m_planes)
    {
        // Corner furthest along the normal; if even that is behind the plane, the box is outside.
        const Vec3 corner(plane.normal.x >= 0.0f ? box.max.x : box.min.x,
                          plane.normal.y >= 0.0f ? box.max.y : box.min.y,
                          plane.normal.z >= 0.0f ? box.max.z : box.min.z);
        if (plane.distance(corner) < 0.0f)
        {
            return false;
        }
    }
    return true;
}

Mat4 perspectiveReverseZ(f32 fovY, f32 aspect, f32 nearPlane, f32 farPlane) noexcept
{
    const f32 f = 1.0f / std::tan(fovY * 0.5f);
    Mat4 m(0.0f);
    m[0][0] = f / aspect;
    m[1][1] = f;
    // depth = (A * z + B) / -z: 1 at z = -near, 0 at z = -far.
    m[2][2] = nearPlane / (farPlane - nearPlane);
    m[2][3] = -1.0f;
    m[3][2] = farPlane * nearPlane / (farPlane - nearPlane);
    return m;
}
} // namespace g7
