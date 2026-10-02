#pragma once

#include <g7/core/Types.hpp>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <format>
#include <string_view>

/// Math types (ADR 0002). Engine code uses these aliases instead of naming glm directly,
/// so a later switch of the math library stays local to this header.
///
/// Coordinate system: right-handed, +Y up, -Z forward, +X right, units in meters.
namespace g7
{
using Vec2 = glm::vec2;
using Vec3 = glm::vec3;
using Vec4 = glm::vec4;
using IVec2 = glm::ivec2;
using IVec3 = glm::ivec3;
using Quat = glm::quat;
using Mat3 = glm::mat3;
using Mat4 = glm::mat4;

inline const Vec3 kWorldUp{0.0f, 1.0f, 0.0f};
inline const Vec3 kWorldForward{0.0f, 0.0f, -1.0f};
inline const Vec3 kWorldRight{1.0f, 0.0f, 0.0f};

inline constexpr f32 kPi = 3.14159265358979323846f;
inline constexpr f32 kEpsilon = 1e-5f;

[[nodiscard]] constexpr f32 toRadians(f32 degrees) noexcept
{
    return degrees * (kPi / 180.0f);
}
[[nodiscard]] constexpr f32 toDegrees(f32 radians) noexcept
{
    return radians * (180.0f / kPi);
}

[[nodiscard]] bool nearlyEqual(f32 a, f32 b, f32 epsilon = kEpsilon) noexcept;
[[nodiscard]] bool nearlyEqual(const Vec3& a, const Vec3& b, f32 epsilon = kEpsilon) noexcept;
/// Compares rotations: q and -q describe the same rotation and compare equal.
[[nodiscard]] bool nearlyEqual(const Quat& a, const Quat& b, f32 epsilon = kEpsilon) noexcept;

/// Builds a rotation from Euler angles in radians, applied as yaw (around +Y), then pitch
/// (around +X), then roll (around +Z): q = yaw * pitch * roll. Positive yaw turns -Z
/// towards -X (left), positive pitch turns -Z towards +Y (up).
[[nodiscard]] Quat quatFromEuler(f32 pitch, f32 yaw, f32 roll) noexcept;

/// Rotation that maps kWorldForward onto `forward`, keeping `up` as close as possible.
/// Falls back to a different up axis when `forward` is parallel to `up`.
[[nodiscard]] Quat lookRotation(const Vec3& forward, const Vec3& up = kWorldUp) noexcept;
} // namespace g7

/// Vectors format as "(x, y, z)"; format specs apply to every component, e.g. "{:.2f}".
template <glm::length_t N, glm::qualifier Q>
struct std::formatter<glm::vec<N, float, Q>> : std::formatter<float>
{
    template <typename FormatContext>
    auto format(const glm::vec<N, float, Q>& v, FormatContext& ctx) const
    {
        auto out = ctx.out();
        *out++ = '(';
        for (glm::length_t i = 0; i < N; ++i)
        {
            if (i > 0)
            {
                *out++ = ',';
                *out++ = ' ';
            }
            ctx.advance_to(out);
            out = std::formatter<float>::format(v[i], ctx);
        }
        *out++ = ')';
        return out;
    }
};

/// Quaternions format as "quat(w, x, y, z)", matching the glm constructor order.
template <glm::qualifier Q>
struct std::formatter<glm::qua<float, Q>> : std::formatter<float>
{
    template <typename FormatContext>
    auto format(const glm::qua<float, Q>& q, FormatContext& ctx) const
    {
        auto out = ctx.out();
        for (const char c : std::string_view("quat("))
        {
            *out++ = c;
        }
        const float components[] = {q.w, q.x, q.y, q.z};
        for (int i = 0; i < 4; ++i)
        {
            if (i > 0)
            {
                *out++ = ',';
                *out++ = ' ';
            }
            ctx.advance_to(out);
            out = std::formatter<float>::format(components[i], ctx);
        }
        *out++ = ')';
        return out;
    }
};
