#pragma once

// Rain (M17 weather part R, docs/design/wetter.md): a curtain of streaks around the camera and splashes on
// the ground, both made in the vertex shader from the instance number and the time (no CPU simulation),
// world-stable (the drops do not move with the camera). A depth map seen from above (the "rain map", 64 x 64
// m around the camera) keeps the drops out from under roofs, gateways and eaves and puts the splashes on the
// top surfaces.

#include <g7/core/Math.hpp>
#include <g7/core/Result.hpp>
#include <g7/render/rhi/Resources.hpp>

#include <optional>
#include <string_view>

namespace g7::render
{
class Device;
class ShaderLibrary;
struct Camera;

enum class RainQuality : u8
{
    Low, ///< fewer streaks, no splashes
    Medium,
    High,
};
[[nodiscard]] std::optional<RainQuality> parseRainQuality(std::string_view text) noexcept;
/// Streaks at full rain for the quality (low 4 000, medium 12 000, high 24 000).
[[nodiscard]] u32 rainStreaks(RainQuality quality) noexcept;

struct RainFrame
{
    f32 rain = 0.0f;  ///< 0..1 (world::WeatherSample)
    Vec2 wind{0.0f};  ///< m/s in world xz, slants the streaks
    f32 time = 0.0f;  ///< seconds
    Vec3 light{0.3f}; ///< sky and sun light on the drops (linear)
    RainQuality quality = RainQuality::Medium;
};

/// The map from above: world xz around `centre` (half size kRainMapRadius) to uv, height to depth 0 (top)
/// .. 1.
inline constexpr f32 kRainMapRadius = 32.0f;
inline constexpr f32 kRainMapHeight = 120.0f; ///< metres from the map's eye downwards
/// The view-projection of the rain map around `centre` (its eye kRainMapHeight / 2 above the centre).
[[nodiscard]] Mat4 rainMapViewProjection(const Vec3& centre) noexcept;

class RainRenderer
{
public:
    RainRenderer() = default;
    [[nodiscard]] static Result<RainRenderer> create(Device& device, ShaderLibrary& shaders,
                                                     u32 mapSize = 512);

    /// True if the map must be drawn anew for the camera at `eye` (none yet, or more than 8 m from its
    /// centre).
    [[nodiscard]] bool mapStale(const Vec3& eye) const noexcept;
    /// Binds and clears the map around `eye` (snapped to a 4 m grid); draw the occluders with
    /// mapViewProjection() and the shadow pipelines (depth 0 near .. 1 far), then endMap().
    void beginMap(Device& device, const Vec3& eye);
    void endMap() noexcept { m_mapValid = true; }
    void invalidateMap() noexcept { m_mapValid = false; }
    [[nodiscard]] const Mat4& mapViewProjection() const noexcept { return m_mapViewProjection; }
    [[nodiscard]] bool mapValid() const noexcept { return m_mapValid; }
    [[nodiscard]] const rhi::Texture& map() const noexcept { return m_map; }

    /// Draws streaks and splashes into the bound target (depth-tested, additive). Returns the streaks drawn.
    u32 draw(Device& device, const Camera& camera, const RainFrame& frame);

private:
    rhi::ShaderProgram* m_program = nullptr; // owned by the ShaderLibrary
    rhi::Pipeline m_pipeline;
    rhi::Texture m_map;
    rhi::Framebuffer m_mapTarget;
    rhi::Sampler m_mapSampler;
    Mat4 m_mapViewProjection{1.0f};
    Vec3 m_mapCentre{0.0f};
    bool m_mapValid = false;
};
} // namespace g7::render
