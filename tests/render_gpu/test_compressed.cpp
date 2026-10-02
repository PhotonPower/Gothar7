// Block-compressed textures (BC7, BC5) on a real driver (label "gpu"). The blocks are built by hand
// from the format specifications; reading back as floats lets the driver decompress them.

#include "GlFixture.hpp"

#include <array>
#include <cmath>

using namespace g7;
using namespace g7::render;
using namespace g7::render::rhi;
using g7::test::GlFixture;
using g7::test::require;

namespace
{
/// Writes bit fields into a 128-bit block, least significant bit first (BC7 layout).
struct BitWriter
{
    std::array<u8, 16> bytes{};
    u32 position = 0;

    void put(u32 value, u32 bits)
    {
        for (u32 i = 0; i < bits; ++i, ++position)
        {
            if ((value >> i) & 1u)
            {
                bytes[position / 8] |= static_cast<u8>(1u << (position % 8));
            }
        }
    }
};

/// BC7 mode 6 block of one colour: both endpoints equal, every index 0. Each channel is a 7-bit
/// endpoint plus a shared p-bit: value = (endpoint << 1) | p.
std::array<u8, 16> solidBc7(u8 r7, u8 g7, u8 b7, u8 a7, u32 pBit)
{
    BitWriter w;
    w.put(1u << 6, 7); // mode 6: six zero bits, then a one
    for (const u8 channel : {r7, g7, b7, a7})
    {
        w.put(channel, 7); // endpoint 0
        w.put(channel, 7); // endpoint 1
    }
    w.put(pBit, 1);
    w.put(pBit, 1);
    w.put(0, 3 + 15 * 4); // indices: anchor with 3 bits, the others with 4
    REQUIRE(w.position == 128);
    return w.bytes;
}

/// BC4 half of a BC5 block: two endpoints and sixteen 3-bit indices, all equal to `index`.
void bc4(std::array<u8, 16>& block, usize offset, u8 e0, u8 e1, u32 index)
{
    block[offset] = e0;
    block[offset + 1] = e1;
    u64 indices = 0;
    for (u32 i = 0; i < 16; ++i)
    {
        indices |= static_cast<u64>(index) << (3 * i);
    }
    for (u32 i = 0; i < 6; ++i)
    {
        block[offset + 2 + i] = static_cast<u8>(indices >> (8 * i));
    }
}

void checkPixel(const std::vector<f32>& pixels, usize index, const Vec4& expected)
{
    for (usize c = 0; c < 4; ++c)
    {
        CHECK(pixels[index * 4 + c] == doctest::Approx(expected[static_cast<int>(c)]).epsilon(0.01));
    }
}
} // namespace

TEST_CASE("Compressed textures: a BC7 block decodes to its colour")
{
    GlFixture gl;
    Texture texture = require(gl.device->createTexture({4, 4, Format::BC7, 1}));
    // Red 127 with p-bit 1 -> 255; green/blue 0 with p-bit 1 -> 1; alpha 127 -> 255.
    const auto block = solidBc7(127, 0, 0, 127, 1);
    REQUIRE(texture.upload(0, block).ok());
    const auto pixels = gl.device->readTextureFloat(texture, 0);
    REQUIRE(pixels.size() == 16 * 4);
    checkPixel(pixels, 0, Vec4(1.0f, 1.0f / 255.0f, 1.0f / 255.0f, 1.0f));
    checkPixel(pixels, 15, Vec4(1.0f, 1.0f / 255.0f, 1.0f / 255.0f, 1.0f));
    CHECK(gl.device->debugErrorCount() == 0);
}

TEST_CASE("Compressed textures: a BC5 block decodes to two channels")
{
    GlFixture gl;
    Texture texture = require(gl.device->createTexture({4, 4, Format::BC5, 1}));
    std::array<u8, 16> block{};
    bc4(block, 0, 200, 50, 0); // red: every index 0 -> endpoint 0 = 200
    bc4(block, 8, 100, 30, 1); // green: every index 1 -> endpoint 1 = 30
    REQUIRE(texture.upload(0, block).ok());
    const auto pixels = gl.device->readTextureFloat(texture, 0);
    REQUIRE(pixels.size() == 16 * 4);
    checkPixel(pixels, 5, Vec4(200.0f / 255.0f, 30.0f / 255.0f, 0.0f, 1.0f));
    CHECK(gl.device->debugErrorCount() == 0);
}

TEST_CASE("Compressed textures: mip levels are uploaded one by one")
{
    GlFixture gl;
    // 8x8 with 4 levels: 2x2 blocks, then one block each for 4x4, 2x2 and 1x1.
    Texture texture = require(gl.device->createTexture({8, 8, Format::BC7_SRGB, 0}));
    REQUIRE(texture.desc().mipLevels == 4);
    std::vector<u8> level0;
    for (int i = 0; i < 4; ++i)
    {
        const auto block = solidBc7(64, 64, 64, 127, 0);
        level0.insert(level0.end(), block.begin(), block.end());
    }
    REQUIRE(texture.upload(0, level0).ok());
    for (u32 level = 1; level < 4; ++level)
    {
        const auto block = solidBc7(static_cast<u8>(level * 30), 0, 0, 127, 0);
        REQUIRE(texture.upload(level, block).ok());
    }
    // Uploaded data is stored as given; mips cannot be generated for block formats.
    CHECK_FALSE(texture.generateMipmaps().ok());
    CHECK(gl.device->debugErrorCount() == 0);
}

TEST_CASE("Compressed textures: wrong sizes and render targets are rejected")
{
    GlFixture gl;
    Texture texture = require(gl.device->createTexture({8, 4, Format::BC5, 1}));
    CHECK_FALSE(texture.upload(0, std::vector<u8>(16)).ok()); // needs 2 blocks = 32 bytes
    CHECK(texture.upload(0, std::vector<u8>(32)).ok());
    CHECK_FALSE(gl.device->createFramebuffer({{&texture}, nullptr}).ok());
    CHECK(gl.device->debugErrorCount() == 0);
}
