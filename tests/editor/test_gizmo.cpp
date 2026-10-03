// Editor gizmos (M4): rays, handles hit in screen pixels at any distance, drags projected onto axis,
// plane and ring.

#include <g7/editor/Gizmo.hpp>
#include <g7/render/Camera.hpp>

#include <doctest/doctest.h>

#include <cmath>
#include <numbers>

using namespace g7;
using namespace g7::editor;

namespace
{
/// 800 x 600 view from `eye` looking at `target`.
GizmoView viewFrom(const Vec3& eye, const Vec3& target)
{
    render::Camera camera;
    camera.aspect = 800.0f / 600.0f;
    camera.transform.position = eye;
    camera.transform.rotation = lookRotation(target - eye);
    return {camera.viewProjection(), eye, Vec2(800.0f, 600.0f)};
}

Vec2 pixel(const GizmoView& view, const Vec3& point)
{
    const auto p = project(view, point);
    REQUIRE(p.has_value());
    return *p;
}
} // namespace

TEST_CASE("Gizmo: rays and projection agree; snapping")
{
    const GizmoView view = viewFrom(Vec3(0, 0, 10), Vec3(0.0f));
    const Ray centre = screenRay(view, Vec2(400.0f, 300.0f));
    CHECK(centre.direction.z == doctest::Approx(-1.0f).epsilon(0.001));
    const Vec2 p = pixel(view, Vec3(1.0f, 2.0f, 0.0f));
    const Ray through = screenRay(view, p);
    // The ray through the projected pixel passes the point.
    const Vec3 toPoint = Vec3(1.0f, 2.0f, 0.0f) - through.origin;
    CHECK(glm::length(glm::cross(toPoint, through.direction)) < 1e-3f);
    CHECK(p.y < 300.0f); // +Y up in the world is up on screen (pixels grow downwards)
    CHECK_FALSE(project(view, Vec3(0, 0, 20)).has_value()); // behind the camera

    CHECK(snap(0.74f, 0.5f) == doctest::Approx(0.5f));
    CHECK(snap(0.76f, 0.5f) == doctest::Approx(1.0f));
    CHECK(snap(0.76f, 0.0f) == doctest::Approx(0.76f));
}

TEST_CASE("Gizmo: arms span the same pixels near and far, handles grab within 8 px")
{
    for (const f32 distance : {5.0f, 50.0f, 500.0f})
    {
        const GizmoView view = viewFrom(Vec3(distance, distance * 0.5f, distance), Vec3(0.0f));
        Gizmo gizmo;
        const f32 arm = gizmo.armLength(view);
        const Vec2 centre = pixel(view, Vec3(0.0f));
        const Vec2 tipX = pixel(view, Vec3(arm, 0.0f, 0.0f));
        const Vec2 tipY = pixel(view, Vec3(0.0f, arm, 0.0f));
        CHECK(glm::length(tipY - centre) == doctest::Approx(Gizmo::kArmPixels).epsilon(0.15));

        const Vec2 onX = (centre + tipX * 3.0f) * 0.25f; // three quarters along the X arm
        CHECK(gizmo.hit(view, onX) == GizmoHandle::X);
        const Vec2 across = glm::normalize(Vec2(-(tipX - centre).y, (tipX - centre).x));
        CHECK(gizmo.hit(view, onX + across * 6.0f) == GizmoHandle::X);  // within the tolerance
        CHECK(gizmo.hit(view, onX + across * 30.0f) != GizmoHandle::X); // far off the arm
        CHECK(gizmo.hit(view, Vec2(5.0f, 5.0f)) == GizmoHandle::None);
    }
}

TEST_CASE("Gizmo: plane handles, scale centre, rotation rings")
{
    const GizmoView view = viewFrom(Vec3(0.0f, 20.0f, 0.01f), Vec3(0.0f)); // from above
    Gizmo gizmo;
    const f32 arm = gizmo.armLength(view);
    const f32 middle = (Gizmo::kPlaneFrom + Gizmo::kPlaneTo) * 0.5f * arm;
    CHECK(gizmo.hit(view, pixel(view, Vec3(middle, 0.0f, middle))) == GizmoHandle::PlaneXZ);

    gizmo.mode = GizmoMode::Scale;
    CHECK(gizmo.hit(view, pixel(view, Vec3(0.0f)) + Vec2(3.0f, -2.0f)) == GizmoHandle::Uniform);
    CHECK(gizmo.hit(view, pixel(view, Vec3(arm * 0.8f, 0.0f, 0.0f))) == GizmoHandle::X);

    gizmo.mode = GizmoMode::Rotate;
    // Seen from above, the ring around Y is a circle of arm length in XZ.
    const f32 diagonal = arm * std::sqrt(0.5f);
    CHECK(gizmo.hit(view, pixel(view, Vec3(diagonal, 0.0f, diagonal))) == GizmoHandle::Y);
    CHECK(gizmo.hit(view, pixel(view, Vec3(arm * 0.3f, 0.0f, arm * 0.3f))) == GizmoHandle::None);
}

TEST_CASE("Gizmo: drags project onto the axis, the plane and the ring")
{
    const GizmoView view = viewFrom(Vec3(0.0f, 20.0f, 0.01f), Vec3(0.0f));
    Gizmo gizmo;
    // Along X: the mouse moves to where x = 3 is on screen (and a bit sideways, which is ignored).
    const Vec2 start = pixel(view, Vec3(0.0f));
    const Vec2 toThree = pixel(view, Vec3(3.0f, 0.0f, 0.0f)) + Vec2(0.0f, 7.0f);
    const Vec3 alongX = gizmo.dragTranslate(GizmoHandle::X, view, start, toThree);
    CHECK(alongX.x == doctest::Approx(3.0f).epsilon(0.01));
    CHECK(alongX.y == doctest::Approx(0.0f));
    CHECK(alongX.z == doctest::Approx(0.0f));
    // In the XZ plane: both components follow.
    const Vec3 inPlane =
        gizmo.dragTranslate(GizmoHandle::PlaneXZ, view, start, pixel(view, Vec3(2.0f, 0.0f, -1.5f)));
    CHECK(inPlane.x == doctest::Approx(2.0f).epsilon(0.01));
    CHECK(inPlane.y == doctest::Approx(0.0f).epsilon(0.01));
    CHECK(inPlane.z == doctest::Approx(-1.5f).epsilon(0.01));

    // About Y: from +X to -Z is a quarter turn, positive (right-handed: +X turns towards -Z).
    gizmo.mode = GizmoMode::Rotate;
    const f32 quarter = gizmo.dragRotate(GizmoHandle::Y, view, pixel(view, Vec3(2.0f, 0.0f, 0.0f)),
                                         pixel(view, Vec3(0.0f, 0.0f, -2.0f)));
    CHECK(quarter == doctest::Approx(std::numbers::pi_v<f32> / 2.0f).epsilon(0.01));

    // Scale X: dragging the arm out by one arm length doubles; uniform by moving the mouse up.
    gizmo.mode = GizmoMode::Scale;
    const f32 arm = gizmo.armLength(view);
    const Vec3 doubled = gizmo.dragScale(GizmoHandle::X, view, pixel(view, Vec3(arm, 0.0f, 0.0f)),
                                         pixel(view, Vec3(arm * 2.0f, 0.0f, 0.0f)));
    CHECK(doubled.x == doctest::Approx(2.0f).epsilon(0.02));
    CHECK(doubled.y == 1.0f);
    const Vec3 uniform =
        gizmo.dragScale(GizmoHandle::Uniform, view, Vec2(400, 300), Vec2(400, 300 - Gizmo::kArmPixels));
    CHECK(uniform == Vec3(2.0f));

    // A rotated gizmo (local axes) drags along its own X.
    gizmo.mode = GizmoMode::Translate;
    gizmo.axes = Mat3(glm::angleAxis(std::numbers::pi_v<f32> / 2.0f, Vec3(0, 1, 0))); // local X = world -Z
    const Vec3 local = gizmo.dragTranslate(GizmoHandle::X, view, start, pixel(view, Vec3(0.0f, 0.0f, -2.0f)));
    CHECK(local.z == doctest::Approx(-2.0f).epsilon(0.01));
    CHECK(local.x == doctest::Approx(0.0f).epsilon(0.01));
}
