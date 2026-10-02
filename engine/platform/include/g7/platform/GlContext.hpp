#pragma once

#include <g7/core/Result.hpp>
#include <g7/core/Types.hpp>

#include <memory>

namespace g7::platform
{
class Window;

struct GlContextDesc
{
    i32 major = 4;
    i32 minor = 6;    ///< Preferred version.
    i32 minMinor = 5; ///< Fallback down to 4.<minMinor> (ADR 0003: 4.5 is the minimum).
#ifdef NDEBUG
    bool debug = false;
#else
    bool debug = true; ///< Debug context: enables GL debug output with full messages.
#endif
};

/// OpenGL core context for a window created with GraphicsApi::OpenGL (ADR 0003). Creating and
/// presenting is platform work; all GL calls themselves live in the render module, which loads
/// the functions through procAddress(). Must outlive every GL object; main thread only.
class GlContext
{
public:
    /// Tries major.minor down to major.minMinor; the context is made current.
    [[nodiscard]] static Result<std::unique_ptr<GlContext>> create(Window& window,
                                                                   const GlContextDesc& desc = {});
    ~GlContext();

    GlContext(const GlContext&) = delete;
    GlContext& operator=(const GlContext&) = delete;

    void swapBuffers();
    /// On: adaptive VSync (tear instead of stall when late) if supported, else classic VSync.
    /// Returns false if the driver refuses.
    bool setVSync(bool enabled);

    /// Version that was granted at creation.
    [[nodiscard]] i32 major() const noexcept { return m_major; }
    [[nodiscard]] i32 minor() const noexcept { return m_minor; }

    using ProcAddress = void (*)();
    /// GL function lookup for the loader (glad) in the render module.
    [[nodiscard]] static ProcAddress procAddress(const char* name) noexcept;

private:
    struct Impl;
    GlContext(std::unique_ptr<Impl> impl, i32 major, i32 minor);
    std::unique_ptr<Impl> m_impl;
    i32 m_major;
    i32 m_minor;
};
} // namespace g7::platform
