#pragma once

// The last frame's scene for screen-space reflections on window glass (M17 windows part F3, render.md
// "Spiegelnde Fenster", stage C): glass is drawn in the opaque pass, before the scene it would mirror is
// complete, so it traces its rays through a copy of the previous frame (colour and depth) with that frame's
// camera. Moving things lag a frame behind - invisible in the wavy old panes.

#include <g7/core/Math.hpp>
#include <g7/core/Result.hpp>
#include <g7/core/Types.hpp>
#include <g7/render/Camera.hpp>
#include <g7/render/Device.hpp>
#include <g7/render/ShaderLibrary.hpp>
#include <g7/render/rhi/Resources.hpp>

namespace g7::render
{
/// Ray steps and length of the glass's reflection rays (plan F3: short, 16 steps, at most 30 m).
inline constexpr i32 kGlassSsrSteps = 16;
inline constexpr f32 kGlassSsrDistance = 30.0f;

class ScreenHistory
{
public:
    ScreenHistory() = default;
    [[nodiscard]] static Result<ScreenHistory> create(Device& device, ShaderLibrary& shaders);

    [[nodiscard]] bool ready() const noexcept { return m_copy != nullptr; }
    /// Copies `color` (linear HDR) and `depth` (reverse-Z) of the finished scene, seen by `camera`.
    [[nodiscard]] Result<void> capture(Device& device, const rhi::Texture& color, const rhi::Texture& depth,
                                       const Camera& camera);
    /// Forget the copy (another world, glass reflections off): valid() is false until the next capture.
    void invalidate() noexcept { m_valid = false; }
    [[nodiscard]] bool valid() const noexcept { return m_valid; }
    [[nodiscard]] const rhi::Texture& color() const noexcept { return m_color; }
    [[nodiscard]] const rhi::Texture& depth() const noexcept { return m_depth; } ///< R32F
    [[nodiscard]] const Camera& camera() const noexcept { return m_camera; }
    [[nodiscard]] u32 width() const noexcept { return m_width; }
    [[nodiscard]] u32 height() const noexcept { return m_height; }

private:
    rhi::ShaderProgram* m_copy = nullptr; // owned by the ShaderLibrary
    rhi::Pipeline m_pipeline;
    rhi::Sampler m_pointSampler;
    rhi::Texture m_color;
    rhi::Texture m_depth;
    rhi::Framebuffer m_framebuffer;
    Camera m_camera;
    u32 m_width = 0;
    u32 m_height = 0;
    bool m_valid = false;
};
} // namespace g7::render
