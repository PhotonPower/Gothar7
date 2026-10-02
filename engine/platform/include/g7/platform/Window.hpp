#pragma once

#include <g7/core/Result.hpp>
#include <g7/core/Types.hpp>

#include <memory>
#include <string>
#include <string_view>

namespace g7::platform
{
class Input;

struct Extent
{
    u32 width = 0;
    u32 height = 0;

    constexpr bool operator==(const Extent&) const noexcept = default;
};

enum class WindowMode : u8
{
    Windowed,
    Fullscreen, ///< Borderless at desktop resolution: fast Alt+Tab, no mode switch.
};

/// Graphics API the window is prepared for (OpenGL: GL-capable window, see GlContext).
enum class GraphicsApi : u8
{
    None,
    OpenGL,
};

struct WindowDesc
{
    std::string title = "Gothar";
    Extent size{1600, 900};
    WindowMode mode = WindowMode::Windowed;
    bool resizable = true;
    GraphicsApi graphics = GraphicsApi::None;
    bool vsync = true; ///< Applied by the engine via GlContext::setVSync.
};

/// The game window (ADR 0011: SDL3, hidden behind PImpl). Owns a reference on the SDL video
/// subsystem, so several windows (e.g. tests) can coexist. Main thread only.
class Window
{
public:
    [[nodiscard]] static Result<std::unique_ptr<Window>> create(const WindowDesc& desc);
    ~Window();

    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;

    /// Processes pending OS events and feeds keyboard, mouse and gamepad state into `input`
    /// (call input.beginFrame() first). Returns false once quitting was requested (close button,
    /// OS shutdown, requestClose()); stays false afterwards. Focus loss releases all inputs.
    [[nodiscard]] bool pollEvents(Input& input);
    /// Same, discarding input (tools, tests).
    [[nodiscard]] bool pollEvents();

    /// Size in screen coordinates.
    [[nodiscard]] Extent size() const noexcept;
    /// Framebuffer size in pixels (differs from size() on HiDPI displays).
    [[nodiscard]] Extent pixelSize() const noexcept;
    /// True if the size changed during the last pollEvents().
    [[nodiscard]] bool resizedSinceLastPoll() const noexcept;

    void setSize(Extent size);
    void setMode(WindowMode mode);
    [[nodiscard]] WindowMode mode() const noexcept;
    void setTitle(std::string_view title);
    [[nodiscard]] std::string title() const;

    /// Relative mouse mode for the camera: hidden, captured cursor with unbounded motion.
    /// Returns false if the platform refuses it (logged).
    bool setRelativeMouse(bool enabled);
    [[nodiscard]] bool relativeMouse() const noexcept;

    /// Asks the window to close; the next pollEvents() returns false.
    void requestClose();

private:
    friend class GlContext; // needs the native window
    struct Impl;
    explicit Window(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> m_impl;
};
} // namespace g7::platform
