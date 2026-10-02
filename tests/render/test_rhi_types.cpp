#include <g7/render/rhi/Types.hpp>

#include <doctest/doctest.h>

#include <utility>

using namespace g7;
using namespace g7::render::rhi;

TEST_CASE("RHI: format sizes and properties")
{
    CHECK(bytesPerPixel(Format::R8) == 1);
    CHECK(bytesPerPixel(Format::RG8) == 2);
    CHECK(bytesPerPixel(Format::RGBA8) == 4);
    CHECK(bytesPerPixel(Format::RGBA8_SRGB) == 4);
    CHECK(bytesPerPixel(Format::RGBA16F) == 8);
    CHECK(bytesPerPixel(Format::Depth32F) == 4);
    CHECK(isDepthFormat(Format::Depth24Stencil8));
    CHECK(isDepthFormat(Format::Depth32F));
    CHECK_FALSE(isDepthFormat(Format::R32F));
    CHECK(hasStencil(Format::Depth24Stencil8));
    CHECK_FALSE(hasStencil(Format::Depth32F));
}

TEST_CASE("RHI: vertex and index sizes")
{
    CHECK(vertexFormatSize(VertexFormat::Float1) == 4);
    CHECK(vertexFormatSize(VertexFormat::Float3) == 12);
    CHECK(vertexFormatSize(VertexFormat::Float4) == 16);
    CHECK(vertexFormatSize(VertexFormat::UNorm8x4) == 4);
    CHECK(indexSize(IndexType::U16) == 2);
    CHECK(indexSize(IndexType::U32) == 4);
}

TEST_CASE("RHI: mip chain")
{
    CHECK(mipLevelCount(1, 1) == 1);
    CHECK(mipLevelCount(2, 1) == 2);
    CHECK(mipLevelCount(1024, 512) == 11);
    CHECK(mipLevelCount(1000, 3) == 10);
    CHECK(mipLevelCount(0, 0) == 1);
    CHECK(mipSize(1024, 0) == 1024);
    CHECK(mipSize(1024, 3) == 128);
    CHECK(mipSize(5, 10) == 1);
    CHECK(mipSize(5, 40) == 1);
}

TEST_CASE("RHI: handle ownership")
{
    static int deleted = 0;
    deleted = 0;
    const auto deleter = [](u32) { ++deleted; };
    {
        Handle a(7, deleter);
        CHECK(a.id() == 7);
        CHECK(a.uid() != 0);
        const u64 uid = a.uid();

        Handle b(std::move(a));
        CHECK_FALSE(a);
        CHECK(b.id() == 7);
        CHECK(b.uid() == uid);

        Handle c(8, deleter);
        CHECK(c.uid() != uid);
        c = std::move(b); // deletes 8
        CHECK(deleted == 1);
        CHECK(c.id() == 7);
    }
    CHECK(deleted == 2); // 7 deleted once, the moved-from handles delete nothing
    CHECK_FALSE(Handle());
}
