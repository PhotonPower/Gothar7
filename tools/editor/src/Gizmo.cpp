#include <g7/editor/Gizmo.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>

namespace g7::editor
{
namespace
{
/// Distance from `p` to the segment a..b in the plane.
f32 distanceToSegment(const Vec2& p, const Vec2& a, const Vec2& b) noexcept
{
    const Vec2 ab = b - a;
    const f32 lengthSq = glm::dot(ab, ab);
    const f32 t = lengthSq > 0.0f ? std::clamp(glm::dot(p - a, ab) / lengthSq, 0.0f, 1.0f) : 0.0f;
    return glm::length(p - (a + ab * t));
}

/// Point in the convex polygon (consistent winding) a, b, c, d?
bool insideQuad(const Vec2& p, const Vec2 (&q)[4]) noexcept
{
    f32 sign = 0.0f;
    for (int i = 0; i < 4; ++i)
    {
        const Vec2 e = q[(i + 1) % 4] - q[i];
        const Vec2 r = p - q[i];
        const f32 cross = e.x * r.y - e.y * r.x;
        if (cross != 0.0f)
        {
            if (sign != 0.0f && (cross > 0.0f) != (sign > 0.0f))
            {
                return false;
            }
            sign = cross;
        }
    }
    return true;
}

/// Parameter t of the point on the line origin + t * axis closest to the ray.
std::optional<f32> closestOnAxis(const Ray& ray, const Vec3& origin, const Vec3& axis) noexcept
{
    const Vec3 w = origin - ray.origin;
    const f32 b = glm::dot(axis, ray.direction);
    const f32 denominator = 1.0f - b * b; // both normalised
    if (denominator < 1e-6f)
    {
        return std::nullopt; // looking along the axis
    }
    const f32 d = glm::dot(axis, w);
    const f32 e = glm::dot(ray.direction, w);
    return (b * e - d) / denominator;
}

std::optional<Vec3> intersectPlane(const Ray& ray, const Vec3& point, const Vec3& normal) noexcept
{
    const f32 facing = glm::dot(ray.direction, normal);
    if (std::abs(facing) < 1e-6f)
    {
        return std::nullopt;
    }
    const f32 t = glm::dot(point - ray.origin, normal) / facing;
    return t >= 0.0f ? std::optional<Vec3>(ray.origin + ray.direction * t) : std::nullopt;
}
} // namespace

Ray screenRay(const GizmoView& view, const Vec2& mouse) noexcept
{
    const Vec2 ndc(mouse.x / view.viewport.x * 2.0f - 1.0f, 1.0f - mouse.y / view.viewport.y * 2.0f);
    const Mat4 inverse = glm::inverse(view.viewProjection);
    // Reverse-Z: depth 1 is near, 0 far (render.md); any two depths give the direction.
    const Vec4 nearPoint = inverse * Vec4(ndc, 1.0f, 1.0f);
    const Vec4 farPoint = inverse * Vec4(ndc, 0.5f, 1.0f);
    const Vec3 a = Vec3(nearPoint) / nearPoint.w;
    const Vec3 b = Vec3(farPoint) / farPoint.w;
    return {view.eye, glm::normalize(b - a)};
}

std::optional<Vec2> project(const GizmoView& view, const Vec3& point) noexcept
{
    const Vec4 clip = view.viewProjection * Vec4(point, 1.0f);
    if (clip.w <= 1e-6f)
    {
        return std::nullopt;
    }
    const Vec2 ndc = Vec2(clip) / clip.w;
    return Vec2((ndc.x * 0.5f + 0.5f) * view.viewport.x, (0.5f - ndc.y * 0.5f) * view.viewport.y);
}

f32 snap(f32 value, f32 step) noexcept
{
    return step > 0.0f ? std::round(value / step) * step : value;
}

f32 Gizmo::armLength(const GizmoView& view) const noexcept
{
    // World size of one pixel at the gizmo: project a point one metre to the side of the view direction.
    const Vec3 toGizmo = origin - view.eye;
    const f32 distance = glm::length(toGizmo);
    if (distance < 1e-4f)
    {
        return 1.0f;
    }
    const Vec3 forward = toGizmo / distance;
    const Vec3 side = glm::normalize(std::abs(forward.y) < 0.99f ? glm::cross(forward, Vec3(0, 1, 0))
                                                                 : glm::cross(forward, Vec3(1, 0, 0)));
    const auto a = project(view, origin);
    const auto b = project(view, origin + side);
    if (!a || !b)
    {
        return 1.0f;
    }
    const f32 pixelsPerMetre = glm::length(*b - *a);
    return pixelsPerMetre > 1e-4f ? kArmPixels / pixelsPerMetre : 1.0f;
}

Vec3 Gizmo::axisOf(GizmoHandle handle) const noexcept
{
    switch (handle)
    {
    case GizmoHandle::X:
        return axes[0];
    case GizmoHandle::Y:
        return axes[1];
    case GizmoHandle::Z:
        return axes[2];
    default:
        return Vec3(0.0f);
    }
}

GizmoHandle Gizmo::hit(const GizmoView& view, const Vec2& mouse) const noexcept
{
    const auto centre = project(view, origin);
    if (!centre)
    {
        return GizmoHandle::None;
    }
    const f32 arm = armLength(view);
    GizmoHandle best = GizmoHandle::None;
    f32 bestDistance = kHitPixels;
    const auto consider = [&](GizmoHandle handle, f32 distance)
    {
        if (distance <= bestDistance)
        {
            best = handle;
            bestDistance = distance;
        }
    };
    constexpr GizmoHandle kAxes[3] = {GizmoHandle::X, GizmoHandle::Y, GizmoHandle::Z};

    if (mode == GizmoMode::Rotate)
    {
        // Rings: a circle of arm length around each axis, sampled on screen.
        for (int a = 0; a < 3; ++a)
        {
            const Vec3 u = axes[(a + 1) % 3];
            const Vec3 v = axes[(a + 2) % 3];
            f32 nearest = std::numeric_limits<f32>::max();
            std::optional<Vec2> previous;
            constexpr int kSegments = 64;
            for (int i = 0; i <= kSegments; ++i)
            {
                const f32 angle = 2.0f * std::numbers::pi_v<f32> * f32(i) / kSegments;
                const auto p = project(view, origin + (u * std::cos(angle) + v * std::sin(angle)) * arm);
                if (p && previous)
                {
                    nearest = std::min(nearest, distanceToSegment(mouse, *previous, *p));
                }
                previous = p;
            }
            consider(kAxes[a], nearest);
        }
        return best;
    }

    if (mode == GizmoMode::Scale &&
        glm::all(glm::lessThanEqual(glm::abs(mouse - *centre), Vec2(kUniformPixels))))
    {
        return GizmoHandle::Uniform; // the centre square wins over the arms starting there
    }
    for (int a = 0; a < 3; ++a)
    {
        if (const auto tip = project(view, origin + axes[a] * arm))
        {
            consider(kAxes[a], distanceToSegment(mouse, *centre, *tip));
        }
    }
    if (mode == GizmoMode::Translate && best == GizmoHandle::None)
    {
        // Plane squares between two arms (only when no arm is under the mouse).
        constexpr GizmoHandle kPlanes[3] = {GizmoHandle::PlaneXY, GizmoHandle::PlaneYZ, GizmoHandle::PlaneXZ};
        constexpr int kPairs[3][2] = {{0, 1}, {1, 2}, {0, 2}};
        for (int p = 0; p < 3; ++p)
        {
            const Vec3 u = axes[kPairs[p][0]] * arm;
            const Vec3 v = axes[kPairs[p][1]] * arm;
            const auto q0 = project(view, origin + u * kPlaneFrom + v * kPlaneFrom);
            const auto q1 = project(view, origin + u * kPlaneTo + v * kPlaneFrom);
            const auto q2 = project(view, origin + u * kPlaneTo + v * kPlaneTo);
            const auto q3 = project(view, origin + u * kPlaneFrom + v * kPlaneTo);
            if (q0 && q1 && q2 && q3)
            {
                const Vec2 quad[4] = {*q0, *q1, *q2, *q3};
                if (insideQuad(mouse, quad))
                {
                    return kPlanes[p];
                }
            }
        }
    }
    return best;
}

Vec3 Gizmo::dragTranslate(GizmoHandle handle, const GizmoView& view, const Vec2& start,
                          const Vec2& now) const noexcept
{
    const Ray a = screenRay(view, start);
    const Ray b = screenRay(view, now);
    if (const Vec3 axis = axisOf(handle); axis != Vec3(0.0f))
    {
        const auto t0 = closestOnAxis(a, origin, axis);
        const auto t1 = closestOnAxis(b, origin, axis);
        return t0 && t1 ? axis * (*t1 - *t0) : Vec3(0.0f);
    }
    Vec3 normal(0.0f);
    switch (handle)
    {
    case GizmoHandle::PlaneXY:
        normal = axes[2];
        break;
    case GizmoHandle::PlaneYZ:
        normal = axes[0];
        break;
    case GizmoHandle::PlaneXZ:
        normal = axes[1];
        break;
    default:
        return Vec3(0.0f);
    }
    const auto p0 = intersectPlane(a, origin, normal);
    const auto p1 = intersectPlane(b, origin, normal);
    return p0 && p1 ? *p1 - *p0 : Vec3(0.0f);
}

f32 Gizmo::dragRotate(GizmoHandle handle, const GizmoView& view, const Vec2& start,
                      const Vec2& now) const noexcept
{
    const Vec3 axis = axisOf(handle);
    if (axis == Vec3(0.0f))
    {
        return 0.0f;
    }
    const auto p0 = intersectPlane(screenRay(view, start), origin, axis);
    const auto p1 = intersectPlane(screenRay(view, now), origin, axis);
    if (!p0 || !p1)
    {
        return 0.0f;
    }
    const Vec3 from = *p0 - origin;
    const Vec3 to = *p1 - origin;
    if (glm::length(from) < 1e-5f || glm::length(to) < 1e-5f)
    {
        return 0.0f;
    }
    return std::atan2(glm::dot(axis, glm::cross(from, to)), glm::dot(from, to));
}

Vec3 Gizmo::dragScale(GizmoHandle handle, const GizmoView& view, const Vec2& start,
                      const Vec2& now) const noexcept
{
    const f32 arm = armLength(view);
    if (handle == GizmoHandle::Uniform)
    {
        // Up enlarges: one arm length of mouse travel (kArmPixels) doubles.
        return Vec3(std::max(0.01f, 1.0f + (start.y - now.y) / kArmPixels));
    }
    const Vec3 axis = axisOf(handle);
    if (axis == Vec3(0.0f))
    {
        return Vec3(1.0f);
    }
    const f32 along = glm::dot(dragTranslate(handle, view, start, now), axis);
    Vec3 factor(1.0f);
    const int index = handle == GizmoHandle::X ? 0 : handle == GizmoHandle::Y ? 1 : 2;
    factor[index] = std::max(0.01f, 1.0f + along / arm);
    return factor;
}
} // namespace g7::editor
