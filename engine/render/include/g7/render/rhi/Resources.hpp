#pragma once

// RHI resources (ADR 0003): RAII, move-only, created through render::Device.

#include <g7/core/Math.hpp>
#include <g7/core/Result.hpp>
#include <g7/core/Types.hpp>
#include <g7/render/rhi/Types.hpp>

#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace g7::render::rhi
{
struct BufferDesc
{
    usize size = 0;
    BufferUsage usage = BufferUsage::Static;
    std::span<const u8> initialData; ///< Empty or exactly `size` bytes.
};

class Buffer
{
public:
    Buffer() = default;
    /// Only for BufferUsage::Dynamic; the range must lie inside the buffer.
    [[nodiscard]] Result<void> update(usize offset, std::span<const u8> data);
    [[nodiscard]] usize size() const noexcept { return m_size; }
    [[nodiscard]] BufferUsage usage() const noexcept { return m_usage; }

private:
    friend class render::Device;
    Handle m_handle;
    usize m_size = 0;
    BufferUsage m_usage = BufferUsage::Static;
};

struct TextureDesc
{
    u32 width = 0;
    u32 height = 0;
    Format format = Format::RGBA8;
    u32 mipLevels = 1; ///< 0 = full chain.
    /// > 1: a 2D array texture (sampler2DArray), all layers the same size and format.
    u32 layers = 1;
    /// A 2D array texture even with a single layer (implied by layers > 1).
    bool array = false;

    [[nodiscard]] bool isArray() const noexcept { return array || layers > 1; }
};

class Texture
{
public:
    Texture() = default;
    /// Uploads a whole mip level (of one layer of an array); `data` must hold exactly imageSize(format,
    /// level width, level height) bytes - pixels, or 4x4 blocks for compressed formats.
    [[nodiscard]] Result<void> upload(u32 level, std::span<const u8> data, u32 layer = 0);
    /// Fills levels 1.. from level 0. Not for compressed formats (their mip levels are uploaded).
    [[nodiscard]] Result<void> generateMipmaps();
    [[nodiscard]] const TextureDesc& desc() const noexcept { return m_desc; }

private:
    friend class render::Device;
    Handle m_handle;
    TextureDesc m_desc; ///< mipLevels resolved (never 0).
};

struct SamplerDesc
{
    Filter minFilter = Filter::Linear;
    Filter magFilter = Filter::Linear;
    Filter mipFilter = Filter::Linear;
    Wrap wrapU = Wrap::Repeat;
    Wrap wrapV = Wrap::Repeat;
    f32 maxAnisotropy = 1.0f;         ///< Clamped to the driver limit; only used with linear minFilter.
    std::optional<CompareOp> compare; ///< Depth comparison (shadow maps).
};

class Sampler
{
public:
    Sampler() = default;

private:
    friend class render::Device;
    Handle m_handle;
};

struct ShaderDesc
{
    std::string_view vertexSource;
    std::string_view fragmentSource;
    std::string_view debugName = "shader";
};

class ShaderProgram
{
public:
    ShaderProgram() = default;

    // Unknown uniform names are ignored (they may have been optimised away).
    void setUniform(std::string_view name, i32 value);
    void setUniform(std::string_view name, f32 value);
    void setUniform(std::string_view name, const Vec2& value);
    void setUniform(std::string_view name, const Vec3& value);
    void setUniform(std::string_view name, const Vec4& value);
    void setUniform(std::string_view name, const Mat4& value);
    /// Integer array uniform (`uniform int name[N]`), starting at element 0.
    void setUniform(std::string_view name, std::span<const i32> values);
    /// Float array uniform (`uniform float name[N]`), starting at element 0.
    void setUniform(std::string_view name, std::span<const f32> values);
    [[nodiscard]] const std::string& name() const noexcept { return m_name; }

private:
    friend class render::Device;
    [[nodiscard]] i32 location(std::string_view name);

    Handle m_handle;
    std::string m_name;
    std::unordered_map<std::string, i32> m_locations;
};

struct VertexAttribute
{
    u32 location = 0;
    VertexFormat format = VertexFormat::Float3;
    u32 offset = 0;
    /// 0: the vertex buffer; 1: the instance buffer (advances once per instance,
    /// PipelineDesc::instanceStride).
    u32 binding = 0;
};

/// Depth bias (glPolygonOffset): constant units plus a slope-scaled part. Used by shadow passes
/// against self-shadowing ("acne").
struct DepthBias
{
    f32 constant = 0.0f;
    f32 slope = 0.0f;
};

struct PipelineDesc
{
    const ShaderProgram* program = nullptr; ///< Must outlive the pipeline.
    std::vector<VertexAttribute> attributes;
    u32 vertexStride = 0;
    /// Stride of the instance buffer (binding 1, Device::bindInstanceBuffer); 0 = none.
    u32 instanceStride = 0;
    Topology topology = Topology::Triangles;
    CullMode cull = CullMode::Back; ///< Front faces are counter-clockwise.
    bool depthTest = true;
    bool depthWrite = true;
    CompareOp depthCompare = CompareOp::GreaterEqual; ///< Reverse-Z: larger depth = nearer.
    BlendMode blend = BlendMode::Opaque;
    DepthBias depthBias;
};

class Pipeline
{
public:
    Pipeline() = default;

private:
    friend class render::Device;
    Handle m_vertexArray;
    // Referenced, not copied: a hot-reloaded program (same object, new GL program) is picked up
    // by existing pipelines on their next bind.
    const ShaderProgram* m_program = nullptr;
    u32 m_vertexStride = 0;
    u32 m_instanceStride = 0;
    Topology m_topology = Topology::Triangles;
    CullMode m_cull = CullMode::Back;
    bool m_depthTest = true;
    bool m_depthWrite = true;
    CompareOp m_depthCompare = CompareOp::GreaterEqual;
    BlendMode m_blend = BlendMode::Opaque;
    DepthBias m_depthBias;
};

struct FramebufferDesc
{
    std::vector<const Texture*> colors; ///< Colour formats, all the same size.
    const Texture* depth = nullptr;     ///< Optional depth (or depth-stencil) texture.
};

class Framebuffer
{
public:
    Framebuffer() = default;
    [[nodiscard]] u32 width() const noexcept { return m_width; }
    [[nodiscard]] u32 height() const noexcept { return m_height; }

private:
    friend class render::Device;
    Handle m_handle;
    u32 m_width = 0;
    u32 m_height = 0;
    bool m_hasStencil = false;
};
} // namespace g7::render::rhi
