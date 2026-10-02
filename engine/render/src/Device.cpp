#include <g7/core/Log.hpp>
#include <g7/render/Device.hpp>

#ifndef G7_RENDER_NO_GL
#include <glad/glad.h>
#endif

#include <string_view>

namespace g7::render
{
#ifdef G7_RENDER_NO_GL

Result<std::unique_ptr<Device>> Device::create(GlLoader, bool)
{
    return Error{"render module was built without OpenGL (glad not found); start with --no-render"};
}
Device::~Device() = default;
void Device::beginFrame(u32, u32, const Vec4&)
{
}
std::vector<u8> Device::readPixels(i32, i32, i32, i32) const
{
    return {};
}
void Device::onDebugMessage(u32, u32, u32, DebugSeverity, const char*)
{
}

#else

namespace
{
constexpr i32 kMinMajor = 4;
constexpr i32 kMinMinor = 5;

// glad's loader takes a plain function; the platform loader is handed over through this.
GlLoader g_loader = nullptr;

void* loadProc(const char* name)
{
    return reinterpret_cast<void*>(g_loader(name));
}

std::string glString(GLenum name)
{
    const auto* value = reinterpret_cast<const char*>(glGetString(name));
    return value ? value : "";
}

DebugSeverity toSeverity(GLenum severity) noexcept
{
    switch (severity)
    {
    case GL_DEBUG_SEVERITY_HIGH:
        return DebugSeverity::High;
    case GL_DEBUG_SEVERITY_MEDIUM:
        return DebugSeverity::Medium;
    case GL_DEBUG_SEVERITY_LOW:
        return DebugSeverity::Low;
    default:
        return DebugSeverity::Notification;
    }
}

std::string_view sourceName(u32 source) noexcept
{
    switch (source)
    {
    case GL_DEBUG_SOURCE_API:
        return "api";
    case GL_DEBUG_SOURCE_WINDOW_SYSTEM:
        return "window-system";
    case GL_DEBUG_SOURCE_SHADER_COMPILER:
        return "shader-compiler";
    case GL_DEBUG_SOURCE_THIRD_PARTY:
        return "third-party";
    case GL_DEBUG_SOURCE_APPLICATION:
        return "application";
    default:
        return "other";
    }
}

std::string_view typeName(u32 type) noexcept
{
    switch (type)
    {
    case GL_DEBUG_TYPE_ERROR:
        return "error";
    case GL_DEBUG_TYPE_DEPRECATED_BEHAVIOR:
        return "deprecated";
    case GL_DEBUG_TYPE_UNDEFINED_BEHAVIOR:
        return "undefined-behavior";
    case GL_DEBUG_TYPE_PORTABILITY:
        return "portability";
    case GL_DEBUG_TYPE_PERFORMANCE:
        return "performance";
    case GL_DEBUG_TYPE_MARKER:
        return "marker";
    default:
        return "other";
    }
}

void APIENTRY debugCallback(GLenum source, GLenum type, GLuint id, GLenum severity, GLsizei /*length*/,
                            const GLchar* message, const void* userParam)
{
    auto* device = const_cast<Device*>(static_cast<const Device*>(userParam));
    device->onDebugMessage(source, type, id, toSeverity(severity), message);
}
} // namespace

Result<std::unique_ptr<Device>> Device::create(GlLoader loader, bool debugOutput)
{
    if (!loader)
    {
        return Error{"no OpenGL loader"};
    }
    g_loader = loader;
    if (!gladLoadGLLoader(loadProc))
    {
        return Error{"failed to load OpenGL functions (no current context?)"};
    }

    std::unique_ptr<Device> device(new Device());
    DeviceInfo& info = device->m_info;
    glGetIntegerv(GL_MAJOR_VERSION, &info.major);
    glGetIntegerv(GL_MINOR_VERSION, &info.minor);
    info.vendor = glString(GL_VENDOR);
    info.renderer = glString(GL_RENDERER);
    info.version = glString(GL_VERSION);
    if (info.major < kMinMajor || (info.major == kMinMajor && info.minor < kMinMinor))
    {
        return Error{"OpenGL " + std::to_string(kMinMajor) + "." + std::to_string(kMinMinor) +
                     " required, got " + info.version + " (" + info.renderer + ")"};
    }
    G7_LOG_INFO("render", "OpenGL {}.{} - {} ({})", info.major, info.minor, info.renderer, info.vendor);

    if (debugOutput)
    {
        glEnable(GL_DEBUG_OUTPUT);
        // Synchronous: the callback runs inside the offending GL call, so the stack is useful.
        glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
        glDebugMessageCallback(debugCallback, device.get());
        glDebugMessageControl(GL_DONT_CARE, GL_DONT_CARE, GL_DONT_CARE, 0, nullptr, GL_TRUE);
        G7_LOG_DEBUG("render", "GL debug output enabled");
    }
    return device;
}

Device::~Device()
{
    // The callback holds a pointer to this device.
    if (glDebugMessageCallback)
    {
        glDebugMessageCallback(nullptr, nullptr);
    }
}

void Device::beginFrame(u32 width, u32 height, const Vec4& clearColor)
{
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, static_cast<GLsizei>(width), static_cast<GLsizei>(height));
    glDepthMask(GL_TRUE);
    glStencilMask(0xFF);
    glClearColor(clearColor.r, clearColor.g, clearColor.b, clearColor.a);
    glClearDepth(1.0);
    glClearStencil(0);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
}

std::vector<u8> Device::readPixels(i32 x, i32 y, i32 width, i32 height) const
{
    if (width <= 0 || height <= 0)
    {
        return {};
    }
    std::vector<u8> pixels(static_cast<usize>(width) * static_cast<usize>(height) * 4);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
    glReadBuffer(GL_BACK);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(x, y, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    return pixels;
}

void Device::onDebugMessage(u32 source, u32 type, u32 id, DebugSeverity severity, const char* message)
{
    ++m_debugCounts[static_cast<usize>(severity)];
    if (type == GL_DEBUG_TYPE_ERROR)
    {
        ++m_debugErrors;
    }
    const DebugKind kind = type == GL_DEBUG_TYPE_ERROR         ? DebugKind::Error
                           : type == GL_DEBUG_TYPE_PERFORMANCE ? DebugKind::Performance
                                                               : DebugKind::Other;
    const log::Level level = logLevelFor(severity, kind);
    if (level < log::minLevel())
    {
        return;
    }
    const auto verdict = m_filter.check(id);
    if (verdict == DebugMessageFilter::Verdict::Suppress)
    {
        return;
    }
    log::print(level, "render", "GL {} [{}] #{}: {}{}", typeName(type), sourceName(source), id, message,
               verdict == DebugMessageFilter::Verdict::LogLastTime
                   ? " (further messages with this id suppressed)"
                   : "");
}

#endif

u32 Device::debugMessageCount(DebugSeverity severity) const noexcept
{
    return m_debugCounts[static_cast<usize>(severity)];
}
} // namespace g7::render
