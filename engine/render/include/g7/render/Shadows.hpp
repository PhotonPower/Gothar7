#pragma once

#include <g7/core/Math.hpp>
#include <g7/core/Result.hpp>
#include <g7/render/rhi/Resources.hpp>

#include <span>
#include <vector>

namespace g7::render
{
class Device;
struct Camera;

/// Cascaded shadow maps for the sun (render.md). All cascades share one depth atlas (2x2 tiles).
struct ShadowSettings
{
    u32 cascades = 4;             ///< 1..4
    u32 resolution = 2048;        ///< per cascade; the atlas is 2x2 of these
    f32 distance = 150.0f;        ///< shadows end here (faded over the last 10 %)
    f32 splitLambda = 0.75f;      ///< 0 = uniform splits, 1 = logarithmic
    f32 casterExtension = 200.0f; ///< metres towards the sun that still cast into a cascade
    f32 depthBias = 1.0f;         ///< constant polygon offset in the shadow pass
    f32 slopeBias = 2.0f;         ///< slope-scaled polygon offset in the shadow pass
    f32 normalOffset = 1.5f;      ///< receiver offset along the normal, in shadow-map texels
};

struct Cascade
{
    Mat4 viewProjection;  ///< world -> light clip space (xy -1..1, depth 0..1 = near..far)
    f32 splitNear = 0.0f; ///< view depth where this cascade starts
    f32 splitFar = 0.0f;  ///< view depth where it ends
    f32 texelWorldSize = 0.0f;
    Vec4 atlasRect{0.0f}; ///< uv offset (xy) and size (zw) of the tile in the atlas
};

/// View-depth split points (cascades + 1 values from near plane to distance): mix of
/// logarithmic and uniform splitting by `lambda`.
[[nodiscard]] std::vector<f32> cascadeSplits(f32 nearPlane, f32 distance, u32 cascades, f32 lambda);

/// Light matrices for the camera's frustum slices. Each slice is enclosed in a sphere (so rotating
/// the camera does not resize it) and the matrix is snapped to whole shadow-map texels (so moving
/// the camera does not make edges shimmer).
[[nodiscard]] std::vector<Cascade> computeCascades(const Camera& camera, const Vec3& sunDirection,
                                                   const ShadowSettings& settings);

/// Depth atlas, its depth-only framebuffer and the comparison sampler (hardware PCF).
class ShadowMap
{
public:
    ShadowMap() = default;
    [[nodiscard]] static Result<ShadowMap> create(Device& device, const ShadowSettings& settings);

    /// Binds and clears the atlas.
    void begin(Device& device);
    /// Restricts rendering to the tile of cascade `index`.
    void beginCascade(Device& device, u32 index) const; ///< clears and selects its tile only

    [[nodiscard]] const rhi::Texture& texture() const noexcept { return m_depth; }
    [[nodiscard]] const rhi::Sampler& sampler() const noexcept { return m_sampler; }
    [[nodiscard]] const ShadowSettings& settings() const noexcept { return m_settings; }

private:
    ShadowSettings m_settings;
    rhi::Texture m_depth;
    rhi::Framebuffer m_framebuffer;
    rhi::Sampler m_sampler;
};
} // namespace g7::render
