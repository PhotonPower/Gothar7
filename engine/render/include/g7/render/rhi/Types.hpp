#pragma once

// RHI vocabulary (ADR 0003). No GL types here: the GL mapping lives in render/src/rhi.

#include <g7/core/Types.hpp>

namespace g7::render
{
class Device;
}

namespace g7::render::rhi
{
enum class BufferUsage : u8
{
    Static,  ///< Contents fixed at creation.
    Dynamic, ///< update() allowed.
};

/// Texture formats. Upload data uses the natural layout of the format (RGBA16F: half floats,
/// Depth24Stencil8: packed 24/8, Depth32F: float).
enum class Format : u8
{
    R8,
    RG8,
    RGBA8,
    RGBA8_SRGB,
    RGBA16F,
    R32F,
    Depth24Stencil8,
    Depth32F,
};

enum class VertexFormat : u8
{
    Float1,
    Float2,
    Float3,
    Float4,
    UNorm8x4, ///< 4 bytes normalised to 0..1 (vertex colours).
};

enum class IndexType : u8
{
    U16,
    U32,
};

enum class Topology : u8
{
    Triangles,
    Lines, ///< Debug draw.
};

enum class CullMode : u8
{
    None,
    Back,
    Front,
};

enum class CompareOp : u8
{
    Never,
    Less,
    LessEqual,
    Equal,
    Greater,
    GreaterEqual,
    Always,
};

enum class BlendMode : u8
{
    Opaque,
    Alpha,    ///< src * a + dst * (1 - a)
    Additive, ///< src * a + dst (fire, magic)
};

enum class Filter : u8
{
    Nearest,
    Linear,
};

enum class Wrap : u8
{
    Repeat,
    Clamp,
    Mirror,
};

[[nodiscard]] u32 bytesPerPixel(Format format) noexcept;
[[nodiscard]] bool isDepthFormat(Format format) noexcept;
[[nodiscard]] bool hasStencil(Format format) noexcept;
[[nodiscard]] u32 vertexFormatSize(VertexFormat format) noexcept;
[[nodiscard]] u32 indexSize(IndexType type) noexcept;
/// Levels of a full mip chain: 1024x512 -> 11.
[[nodiscard]] u32 mipLevelCount(u32 width, u32 height) noexcept;
/// Size of mip `level` along one axis (never below 1).
[[nodiscard]] u32 mipSize(u32 size, u32 level) noexcept;

/// Owns one GL object name. Move-only; the deleter runs on destruction. The uid is unique per
/// object for the lifetime of the process, so state caches never confuse a new object with a
/// deleted one that had the same GL name.
class Handle
{
public:
    using Deleter = void (*)(u32 id);

    Handle() noexcept = default;
    Handle(u32 id, Deleter deleter) noexcept;
    ~Handle();
    Handle(Handle&& other) noexcept;
    Handle& operator=(Handle&& other) noexcept;
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;

    [[nodiscard]] u32 id() const noexcept { return m_id; }
    [[nodiscard]] u64 uid() const noexcept { return m_uid; }
    [[nodiscard]] explicit operator bool() const noexcept { return m_id != 0; }

private:
    void reset() noexcept;

    u32 m_id = 0;
    u64 m_uid = 0;
    Deleter m_deleter = nullptr;
};
} // namespace g7::render::rhi
