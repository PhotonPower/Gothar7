#pragma once

// Debug and editor UI on Dear ImGui (ADR 0015). ImGui stays private to this module: callers use
// DebugUi and the panels declared here; the backends feed platform::Input and draw via the RHI.

#include <g7/core/Math.hpp>
#include <g7/core/Result.hpp>
#include <g7/core/Types.hpp>
#include <g7/render/Device.hpp>
#include <g7/render/PostProcess.hpp>

#include <memory>
#include <optional>

namespace g7::platform
{
class Input;
}

namespace g7::render
{
class ShaderLibrary;
}

namespace g7::ui
{
/// Values shown and edited in the engine panel ("Gothar" window). The engine fills it each frame
/// and applies the edited fields afterwards.
struct EnginePanel
{
    // Shown
    render::FrameStats frame;
    Vec3 cameraPosition{0.0f};
    u32 width = 0;
    u32 height = 0;
    u64 simulationTicks = 0;
    // Edited
    f32 fovDegrees = 70.0f;
    f32 flySpeed = 10.0f;
    render::Tonemapper tonemapper = render::Tonemapper::Aces;
    f32 exposure = 1.0f;
    f32 fogStart = 30.0f;
    f32 fogDensity = 0.0f;
    bool sun = true;
    bool shadowDebug = false;
    bool debugDraw = false;
    bool paused = false;
    f32 timeScale = 1.0f;
};

/// Window-space clip rectangle (x0, y0, x1, y1; +Y down, in framebuffer pixels) as a GL scissor
/// rectangle (origin bottom-left), clamped to the framebuffer; nullopt if nothing remains.
[[nodiscard]] std::optional<render::PixelRect> scissorFromClip(const Vec4& clip, u32 framebufferWidth,
                                                               u32 framebufferHeight) noexcept;

class DebugUi
{
public:
    /// One ImGui context. Without a device (nullptr, tests) frames run but nothing is drawn.
    /// `scale` enlarges fonts and sizes for high-DPI displays (Window::displayScale).
    [[nodiscard]] static Result<DebugUi> create(render::Device* device, render::ShaderLibrary* shaders,
                                                f32 scale = 1.0f);

    DebugUi();
    ~DebugUi();
    DebugUi(DebugUi&&) noexcept;
    DebugUi& operator=(DebugUi&&) noexcept;
    DebugUi(const DebugUi&) = delete;
    DebugUi& operator=(const DebugUi&) = delete;

    /// Starts a frame: feeds this frame's input. `size` in window coordinates (the mouse's space),
    /// `pixels` the framebuffer size.
    void beginFrame(const platform::Input& input, Vec2 size, Vec2 pixels, f32 deltaSeconds);
    /// Draws the engine panel; edits are written back into `panel`.
    void enginePanel(EnginePanel& panel);
    /// Finishes the frame and, with a device, draws it into the bound target (normally the window).
    void endFrame(render::Device* device);

    /// ImGui uses the mouse / keyboard / wants text this frame: the game should ignore them.
    [[nodiscard]] bool wantsMouse() const noexcept;
    [[nodiscard]] bool wantsKeyboard() const noexcept;
    [[nodiscard]] bool wantsText() const noexcept;
    [[nodiscard]] bool valid() const noexcept { return m_impl != nullptr; }

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};
} // namespace g7::ui
