#pragma once

// Water surfaces (M17 part W1, render.md "Plan M17"): a pass after the opaque scene and the sky. With the
// quality medium or high it copies the scene's colour and depth, then draws each water surface with light
// refraction, colour by the depth of the water (Beer-Lambert), a soft shore edge, Fresnel between the
// transmitted light and the reflected sky, and a sun glint. Low: the surface without copies (Fresnel and
// glint only). Kinds of water (river, pond) come from data/water.toml; the normals are made in code (no image
// asset).

#include <g7/core/Math.hpp>
#include <g7/core/Result.hpp>
#include <g7/render/rhi/Resources.hpp>

#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace g7::render
{
class Device;
class ShaderLibrary;
class SceneTarget;
struct Camera;

enum class WaterQuality : u8
{
    Low,    ///< no copies: tinted surface with Fresnel and glint
    Medium, ///< refraction and depth colour (copies of colour and depth)
    High,   ///< as medium; W2 adds the screen-space reflection here
};
/// "low" | "medium" | "high"; nullopt for anything else.
[[nodiscard]] std::optional<WaterQuality> parseWaterQuality(std::string_view text) noexcept;

/// One kind of water (data/water.toml).
struct WaterStyle
{
    Vec3 absorption{1.0f, 0.85f, 1.3f}; ///< per metre and channel (Beer-Lambert): higher = murkier
    Vec3 scatter{0.04f, 0.05f, 0.03f};  ///< colour of deep water (linear, lit by sun and sky)
    f32 waveScale = 3.0f;               ///< metres per tile of the wave pattern
    f32 waveSpeed = 0.04f;              ///< tiles per second the two wave layers drift apart
    f32 waveStrength = 0.35f;           ///< tilt of the normals (0 = a mirror)
    f32 flowSpeed = 0.0f;               ///< m/s along the box's local +X (rivers); 0 = standing water
    f32 refraction = 0.02f;             ///< largest offset of the refraction, part of the screen width
    f32 glint = 2.0f;                   ///< strength of the sun's highlight
    f32 shininess = 400.0f;             ///< sharpness of the highlight (Blinn-Phong exponent)
    f32 shoreFade = 0.25f;              ///< metres of water depth over which the shore edge fades in
};

/// The kinds of water: `[kind.<name>]` tables and `default = "<name>"` for water without (or with an unknown)
/// kind. Contract with welt (render.md "Plan M17"): `river` flows along the box's local +X, `pond` stands.
class WaterStyles
{
public:
    [[nodiscard]] static Result<WaterStyles> parse(std::string_view toml, std::string_view source);
    /// The style of `kind`; the default for an empty or unknown kind.
    [[nodiscard]] const WaterStyle& style(std::string_view kind) const;
    [[nodiscard]] bool contains(std::string_view kind) const { return m_kinds.contains(std::string(kind)); }

private:
    std::map<std::string, WaterStyle, std::less<>> m_kinds;
    std::string m_default;
    WaterStyle m_fallback;
};

/// Schlick's approximation: the part of the light a water surface reflects at `cosTheta` between view and
/// normal (f0 = 0.02 for water). Same as water.frag.
[[nodiscard]] f32 waterFresnel(f32 cosTheta, f32 f0 = 0.02f) noexcept;
/// The part of the light that crosses `metres` of water per channel (Beer-Lambert). Same as water.frag.
[[nodiscard]] Vec3 waterTransmittance(const Vec3& absorption, f32 metres) noexcept;
/// The tiling wave height field the normals are made from (size x size texels, RGBA8: xz of the normal in RG,
/// height in B); public for tests.
[[nodiscard]] std::vector<u8> makeWaterNormals(u32 size);

/// One surface: a rectangle at `centre.y`, turned by `yaw` about +Y.
struct WaterSurface
{
    Vec3 centre{0.0f};
    f32 yaw = 0.0f;
    Vec2 halfExtents{1.0f}; ///< local x and z
    const WaterStyle* style = nullptr;
};

/// Light and time for a frame of water.
struct WaterFrame
{
    f32 time = 0.0f; ///< seconds, moves the waves
    WaterQuality quality = WaterQuality::Medium;
    Vec3 sunDirection{0.0f, 1.0f, 0.0f}; ///< towards the sun
    Vec3 sunColor{1.0f};                 ///< times intensity, linear
    Vec3 ambient{0.2f};                  ///< sky light on the water
    Vec3 skyHorizon{0.5f};               ///< the reflected sky (as background.frag)
    Vec3 skyZenith{0.3f, 0.4f, 0.7f};
    Vec3 fogColor{0.5f};
    f32 fogStart = 30.0f;
    f32 fogDensity = 0.0f;
};

class WaterRenderer
{
public:
    WaterRenderer() = default;
    [[nodiscard]] static Result<WaterRenderer> create(Device& device, ShaderLibrary& shaders);
    /// Draws the visible surfaces into `scene` (bound afterwards); copies first unless the quality is low.
    /// Returns the number of surfaces drawn (0: nothing visible, no copy made).
    u32 draw(Device& device, const SceneTarget& scene, const Camera& camera,
             std::span<const WaterSurface> surfaces, const WaterFrame& frame);

private:
    [[nodiscard]] Result<void> resize(Device& device, u32 width, u32 height);

    rhi::ShaderProgram* m_copy = nullptr; // owned by the ShaderLibrary
    rhi::ShaderProgram* m_water = nullptr;
    rhi::Pipeline m_copyPipeline;
    rhi::Pipeline m_waterPipeline;
    rhi::Sampler m_clampSampler;
    rhi::Sampler m_pointSampler;
    rhi::Sampler m_waveSampler;
    rhi::Texture m_normals;
    rhi::Texture m_colorCopy;
    rhi::Texture m_depthCopy;
    rhi::Framebuffer m_copyFramebuffer;
    u32 m_width = 0;
    u32 m_height = 0;
    std::vector<u32> m_visible;
};
} // namespace g7::render
