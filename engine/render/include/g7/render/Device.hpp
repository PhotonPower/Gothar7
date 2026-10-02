#pragma once

#include <g7/core/Math.hpp>
#include <g7/core/Result.hpp>
#include <g7/core/Types.hpp>
#include <g7/render/DebugOutput.hpp>
#include <g7/render/rhi/Resources.hpp>
#include <g7/render/rhi/Types.hpp>

#include <array>
#include <memory>
#include <optional>
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
    f32 maxAnisotropy = 1.0f; ///< 1 if anisotropic filtering is unavailable.
};

/// Counters of the current frame (reset by beginFrame), for the debug overlay.
struct FrameStats
{
    u32 drawCalls = 0;
    u32 triangles = 0;
    u32 pipelineChanges = 0;
    u32 textureBinds = 0;
};

/// Root of the RHI (ADR 0003) and, for now, its immediate context: creates resources and
/// executes binds and draws directly. Redundant state changes are skipped via a state cache.
/// Requires a current GL context that outlives the device and all resources. Main thread only.
class Device
{
public:
    static constexpr u32 kMaxTextureUnits = 16;

    [[nodiscard]] static Result<std::unique_ptr<Device>> create(GlLoader loader, bool debugOutput);
    ~Device();

    Device(const Device&) = delete;
    Device& operator=(const Device&) = delete;

    [[nodiscard]] const DeviceInfo& info() const noexcept { return m_info; }

    // --- Resources ---
    [[nodiscard]] Result<rhi::Buffer> createBuffer(const rhi::BufferDesc& desc);
    [[nodiscard]] Result<rhi::Texture> createTexture(const rhi::TextureDesc& desc);
    [[nodiscard]] Result<rhi::Sampler> createSampler(const rhi::SamplerDesc& desc);
    /// Errors contain the driver's compiler/linker log (with line numbers).
    [[nodiscard]] Result<rhi::ShaderProgram> createShaderProgram(const rhi::ShaderDesc& desc);
    [[nodiscard]] Result<rhi::Pipeline> createPipeline(const rhi::PipelineDesc& desc);
    /// Fails for invalid attachments or an incomplete framebuffer.
    [[nodiscard]] Result<rhi::Framebuffer> createFramebuffer(const rhi::FramebufferDesc& desc);

    // --- Frame and render targets ---
    /// Resets the frame stats, binds the window framebuffer, sets the viewport, clears colour and
    /// depth (to 0: reverse-Z, see perspectiveReverseZ).
    void beginFrame(u32 width, u32 height, const Vec4& clearColor);
    /// nullptr = window framebuffer.
    void bindFramebuffer(const rhi::Framebuffer* framebuffer);
    void setViewport(i32 x, i32 y, u32 width, u32 height);
    /// Clears the bound framebuffer; depth also clears stencil to 0.
    void clear(std::optional<Vec4> color, std::optional<f32> depth);

    // --- State and draws ---
    void bindPipeline(const rhi::Pipeline& pipeline);
    /// Applies to the bound pipeline's vertex layout.
    void bindVertexBuffer(const rhi::Buffer& buffer, usize offset = 0);
    void bindIndexBuffer(const rhi::Buffer& buffer, rhi::IndexType type);
    void bindTexture(u32 unit, const rhi::Texture& texture, const rhi::Sampler& sampler);
    /// Uniform block binding point (GLSL `layout(binding = slot)`).
    void bindUniformBuffer(u32 slot, const rhi::Buffer& buffer);
    void draw(u32 vertexCount, u32 firstVertex = 0);
    void drawIndexed(u32 indexCount, u32 firstIndex = 0);

    // --- Readback (tests now; screenshots/thumbnails later) ---
    /// RGBA8, bottom row first; colour attachment 0 of `framebuffer`, or the window's back buffer.
    [[nodiscard]] std::vector<u8> readPixels(i32 x, i32 y, i32 width, i32 height,
                                             const rhi::Framebuffer* framebuffer = nullptr) const;
    [[nodiscard]] std::vector<u8> readBuffer(const rhi::Buffer& buffer, usize offset, usize size) const;
    /// Contents of one mip level (RGBA8 formats only, rows as stored); empty for other formats.
    [[nodiscard]] std::vector<u8> readTexture(const rhi::Texture& texture, u32 level) const;

    [[nodiscard]] const FrameStats& stats() const noexcept { return m_stats; }

    /// Number of GL debug messages received per severity (including suppressed ones).
    [[nodiscard]] u32 debugMessageCount(DebugSeverity severity) const noexcept;
    /// Number of GL errors (GL_DEBUG_TYPE_ERROR) reported through the debug output.
    [[nodiscard]] u32 debugErrorCount() const noexcept { return m_debugErrors; }

    /// Internal: called by the GL debug callback.
    void onDebugMessage(u32 source, u32 type, u32 id, DebugSeverity severity, const char* message);

private:
    Device() = default;

    struct BoundTexture
    {
        u64 texture = 0;
        u64 sampler = 0;
    };

    /// Cached GL state; `valid` false forces the next bindPipeline to set everything.
    struct StateCache
    {
        bool valid = false;
        u64 pipeline = 0;
        u32 vertexArray = 0; // GL name of the bound pipeline's VAO (for vertex/index buffer binds)
        u32 vertexStride = 0;
        u64 program = 0;
        rhi::CullMode cull = rhi::CullMode::None;
        bool depthTest = false;
        bool depthWrite = true;
        rhi::CompareOp depthCompare = rhi::CompareOp::Less;
        rhi::BlendMode blend = rhi::BlendMode::Opaque;
        rhi::DepthBias depthBias{};
        rhi::Topology topology = rhi::Topology::Triangles;
        rhi::IndexType indexType = rhi::IndexType::U32;
        std::array<BoundTexture, kMaxTextureUnits> textures{};
    };

    DeviceInfo m_info;
    DebugMessageFilter m_filter;
    std::array<u32, 4> m_debugCounts{};
    u32 m_debugErrors = 0;
    FrameStats m_stats;
    StateCache m_cache;
    bool m_framebufferHasStencil = true; // the window framebuffer has 8 stencil bits
};
} // namespace g7::render
