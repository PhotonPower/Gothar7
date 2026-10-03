#pragma once

// Internal: RHI enums -> OpenGL enums. Only included by GL translation units.

#include <g7/render/rhi/Types.hpp>

#include <glad/glad.h>

namespace g7::render::rhi::gl
{
struct TextureFormat
{
    GLenum internalFormat;
    GLenum format;
    GLenum type;
};

inline TextureFormat textureFormat(Format format) noexcept
{
    switch (format)
    {
    case Format::R8:
        return {GL_R8, GL_RED, GL_UNSIGNED_BYTE};
    case Format::RG8:
        return {GL_RG8, GL_RG, GL_UNSIGNED_BYTE};
    case Format::R16:
        return {GL_R16, GL_RED, GL_UNSIGNED_SHORT};
    case Format::RGBA8:
        return {GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE};
    case Format::RGBA8_SRGB:
        return {GL_SRGB8_ALPHA8, GL_RGBA, GL_UNSIGNED_BYTE};
    case Format::RGBA16F:
        return {GL_RGBA16F, GL_RGBA, GL_HALF_FLOAT};
    case Format::R32F:
        return {GL_R32F, GL_RED, GL_FLOAT};
    case Format::Depth24Stencil8:
        return {GL_DEPTH24_STENCIL8, GL_DEPTH_STENCIL, GL_UNSIGNED_INT_24_8};
    case Format::Depth32F:
        return {GL_DEPTH_COMPONENT32F, GL_DEPTH_COMPONENT, GL_FLOAT};
    case Format::BC7:
        return {GL_COMPRESSED_RGBA_BPTC_UNORM, GL_RGBA, GL_UNSIGNED_BYTE};
    case Format::BC7_SRGB:
        return {GL_COMPRESSED_SRGB_ALPHA_BPTC_UNORM, GL_RGBA, GL_UNSIGNED_BYTE};
    case Format::BC5:
        return {GL_COMPRESSED_RG_RGTC2, GL_RG, GL_UNSIGNED_BYTE};
    }
    return {GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE};
}

struct VertexAttribFormat
{
    GLint components;
    GLenum type;
    GLboolean normalized;
};

inline VertexAttribFormat vertexFormat(VertexFormat format) noexcept
{
    switch (format)
    {
    case VertexFormat::Float1:
        return {1, GL_FLOAT, GL_FALSE};
    case VertexFormat::Float2:
        return {2, GL_FLOAT, GL_FALSE};
    case VertexFormat::Float3:
        return {3, GL_FLOAT, GL_FALSE};
    case VertexFormat::Float4:
        return {4, GL_FLOAT, GL_FALSE};
    case VertexFormat::UNorm8x4:
        return {4, GL_UNSIGNED_BYTE, GL_TRUE};
    case VertexFormat::UInt1:
        return {1, GL_UNSIGNED_INT, GL_FALSE};
    case VertexFormat::UInt16x4:
        return {4, GL_UNSIGNED_SHORT, GL_FALSE};
    }
    return {3, GL_FLOAT, GL_FALSE};
}

inline GLenum compareOp(CompareOp op) noexcept
{
    switch (op)
    {
    case CompareOp::Never:
        return GL_NEVER;
    case CompareOp::Less:
        return GL_LESS;
    case CompareOp::LessEqual:
        return GL_LEQUAL;
    case CompareOp::Equal:
        return GL_EQUAL;
    case CompareOp::Greater:
        return GL_GREATER;
    case CompareOp::GreaterEqual:
        return GL_GEQUAL;
    case CompareOp::Always:
        return GL_ALWAYS;
    }
    return GL_LESS;
}

inline GLenum topology(Topology topology) noexcept
{
    return topology == Topology::Lines ? GL_LINES : GL_TRIANGLES;
}

inline GLenum indexType(IndexType type) noexcept
{
    return type == IndexType::U16 ? GL_UNSIGNED_SHORT : GL_UNSIGNED_INT;
}

inline GLenum wrap(Wrap wrap) noexcept
{
    switch (wrap)
    {
    case Wrap::Clamp:
        return GL_CLAMP_TO_EDGE;
    case Wrap::Mirror:
        return GL_MIRRORED_REPEAT;
    default:
        return GL_REPEAT;
    }
}

inline GLenum minFilter(Filter min, Filter mip, bool hasMips) noexcept
{
    if (!hasMips)
    {
        return min == Filter::Linear ? GL_LINEAR : GL_NEAREST;
    }
    if (min == Filter::Linear)
    {
        return mip == Filter::Linear ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR_MIPMAP_NEAREST;
    }
    return mip == Filter::Linear ? GL_NEAREST_MIPMAP_LINEAR : GL_NEAREST_MIPMAP_NEAREST;
}

// Not in the glad core headers below 4.6; same value as GL_EXT_texture_filter_anisotropic.
inline constexpr GLenum kTextureMaxAnisotropy = 0x84FE;
inline constexpr GLenum kMaxTextureMaxAnisotropy = 0x84FF;
} // namespace g7::render::rhi::gl
