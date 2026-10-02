#pragma once

#include <g7/core/Math.hpp>
#include <g7/core/Result.hpp>
#include <g7/core/Types.hpp>
#include <g7/render/DebugOutput.hpp>

#include <array>
#include <memory>
#include <string>
#include <vector>

namespace g7::render
{
/// GL function lookup supplied by the platform (platform::GlContext::procAddress).
using GlProc = void (*)();
using GlLoader = GlProc (*)(const char* name);

struct DeviceInfo
{
    std::string vendor;
    std::string renderer;
    std::string version;
    i32 major = 0;
    i32 minor = 0;
};

/// Root of the RHI (ADR 0003): loads OpenGL with glad into the current context, checks the
/// version (>= 4.5 core) and routes GL debug output into the log. Requires a current context
/// that outlives the device. Main thread only.
class Device
{
public:
    [[nodiscard]] static Result<std::unique_ptr<Device>> create(GlLoader loader, bool debugOutput);
    ~Device();

    Device(const Device&) = delete;
    Device& operator=(const Device&) = delete;

    [[nodiscard]] const DeviceInfo& info() const noexcept { return m_info; }

    /// Binds the default framebuffer, sets the viewport and clears colour/depth/stencil.
    void beginFrame(u32 width, u32 height, const Vec4& clearColor);

    /// RGBA8 pixels of the default framebuffer, bottom row first (GL convention). Used for
    /// tests now, screenshots / save thumbnails later.
    [[nodiscard]] std::vector<u8> readPixels(i32 x, i32 y, i32 width, i32 height) const;

    /// Number of GL debug messages received per severity (including suppressed ones).
    [[nodiscard]] u32 debugMessageCount(DebugSeverity severity) const noexcept;
    /// Number of GL errors (GL_DEBUG_TYPE_ERROR) reported through the debug output.
    [[nodiscard]] u32 debugErrorCount() const noexcept { return m_debugErrors; }

    /// Internal: called by the GL debug callback.
    void onDebugMessage(u32 source, u32 type, u32 id, DebugSeverity severity, const char* message);

private:
    Device() = default;

    DeviceInfo m_info;
    DebugMessageFilter m_filter;
    std::array<u32, 4> m_debugCounts{};
    u32 m_debugErrors = 0;
};
} // namespace g7::render
