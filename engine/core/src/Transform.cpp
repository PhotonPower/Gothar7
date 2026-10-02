#include <g7/core/Transform.hpp>

#include <glm/gtc/matrix_transform.hpp>

namespace g7
{
Mat4 Transform::toMatrix() const noexcept
{
    Mat4 matrix = glm::mat4_cast(rotation);
    matrix[0] *= scale.x;
    matrix[1] *= scale.y;
    matrix[2] *= scale.z;
    matrix[3] = Vec4(position, 1.0f);
    return matrix;
}

Transform Transform::fromMatrix(const Mat4& matrix) noexcept
{
    Transform result;
    result.position = Vec3(matrix[3]);

    Vec3 axes[3] = {Vec3(matrix[0]), Vec3(matrix[1]), Vec3(matrix[2])};
    result.scale = Vec3(glm::length(axes[0]), glm::length(axes[1]), glm::length(axes[2]));
    if (glm::determinant(Mat3(matrix)) < 0.0f)
    {
        result.scale.x = -result.scale.x;
    }
    for (int i = 0; i < 3; ++i)
    {
        if (result.scale[i] != 0.0f)
        {
            axes[i] /= result.scale[i];
        }
    }
    result.rotation = glm::normalize(glm::quat_cast(Mat3(axes[0], axes[1], axes[2])));
    return result;
}

Transform Transform::operator*(const Transform& child) const noexcept
{
    Transform result;
    result.position = transformPoint(child.position);
    result.rotation = glm::normalize(rotation * child.rotation);
    result.scale = scale * child.scale;
    return result;
}

Transform Transform::inverse() const noexcept
{
    Transform result;
    result.rotation = glm::conjugate(rotation);
    result.scale = Vec3(1.0f) / scale;
    result.position = result.scale * (result.rotation * -position);
    return result;
}

Vec3 Transform::transformPoint(const Vec3& point) const noexcept
{
    return position + rotation * (scale * point);
}

Vec3 Transform::transformDirection(const Vec3& direction) const noexcept
{
    return rotation * direction;
}

Vec3 Transform::forward() const noexcept
{
    return rotation * kWorldForward;
}

Vec3 Transform::right() const noexcept
{
    return rotation * kWorldRight;
}

Vec3 Transform::up() const noexcept
{
    return rotation * kWorldUp;
}

Transform interpolate(const Transform& a, const Transform& b, f32 t) noexcept
{
    Transform result;
    result.position = glm::mix(a.position, b.position, t);
    result.rotation = glm::normalize(glm::slerp(a.rotation, b.rotation, t));
    result.scale = glm::mix(a.scale, b.scale, t);
    return result;
}
} // namespace g7
