#include <g7/render/rhi/Types.hpp>

#include <algorithm>
#include <atomic>
#include <bit>

namespace g7::render::rhi
{
u32 bytesPerPixel(Format format) noexcept
{
    switch (format)
    {
    case Format::R8:
        return 1;
    case Format::RG8:
    case Format::R16:
        return 2;
    case Format::RGBA8:
    case Format::RGBA8_SRGB:
    case Format::R32F:
    case Format::Depth24Stencil8:
    case Format::Depth32F:
        return 4;
    case Format::RGBA16F:
        return 8;
    case Format::BC7:
    case Format::BC7_SRGB:
    case Format::BC5:
        return 0; // block-compressed: see imageSize
    }
    return 0;
}

bool isDepthFormat(Format format) noexcept
{
    return format == Format::Depth24Stencil8 || format == Format::Depth32F;
}

bool hasStencil(Format format) noexcept
{
    return format == Format::Depth24Stencil8;
}

bool isCompressed(Format format) noexcept
{
    return format == Format::BC7 || format == Format::BC7_SRGB || format == Format::BC5;
}

usize imageSize(Format format, u32 width, u32 height) noexcept
{
    if (isCompressed(format))
    {
        constexpr usize kBlockBytes = 16; // BC5 and BC7 alike
        return static_cast<usize>((width + 3) / 4) * ((height + 3) / 4) * kBlockBytes;
    }
    return static_cast<usize>(width) * height * bytesPerPixel(format);
}

u32 vertexFormatSize(VertexFormat format) noexcept
{
    switch (format)
    {
    case VertexFormat::Float1:
        return 4;
    case VertexFormat::Float2:
        return 8;
    case VertexFormat::Float3:
        return 12;
    case VertexFormat::Float4:
        return 16;
    case VertexFormat::UNorm8x4:
        return 4;
    }
    return 0;
}

u32 indexSize(IndexType type) noexcept
{
    return type == IndexType::U16 ? 2u : 4u;
}

u32 mipLevelCount(u32 width, u32 height) noexcept
{
    const u32 largest = std::max({width, height, 1u});
    return static_cast<u32>(std::bit_width(largest));
}

u32 mipSize(u32 size, u32 level) noexcept
{
    return level >= 32 ? 1u : std::max(1u, size >> level);
}

namespace
{
std::atomic<u64> g_nextUid{1};
} // namespace

Handle::Handle(u32 id, Deleter deleter) noexcept
    : m_id(id), m_uid(id != 0 ? g_nextUid.fetch_add(1, std::memory_order_relaxed) : 0), m_deleter(deleter)
{
}

Handle::~Handle()
{
    reset();
}

Handle::Handle(Handle&& other) noexcept : m_id(other.m_id), m_uid(other.m_uid), m_deleter(other.m_deleter)
{
    other.m_id = 0;
    other.m_uid = 0;
}

Handle& Handle::operator=(Handle&& other) noexcept
{
    if (this != &other)
    {
        reset();
        m_id = other.m_id;
        m_uid = other.m_uid;
        m_deleter = other.m_deleter;
        other.m_id = 0;
        other.m_uid = 0;
    }
    return *this;
}

void Handle::reset() noexcept
{
    if (m_id != 0 && m_deleter)
    {
        m_deleter(m_id);
    }
    m_id = 0;
    m_uid = 0;
}
} // namespace g7::render::rhi
