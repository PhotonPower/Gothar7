#include <g7/core/Crc32.hpp>

#include <array>

namespace g7
{
namespace
{
constexpr std::array<u32, 256> makeTable() noexcept
{
    std::array<u32, 256> table{};
    for (u32 i = 0; i < 256; ++i)
    {
        u32 c = i;
        for (int k = 0; k < 8; ++k)
        {
            c = (c & 1u) != 0 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
        }
        table[i] = c;
    }
    return table;
}

constexpr std::array<u32, 256> kTable = makeTable();
} // namespace

u32 crc32(std::span<const u8> data, u32 crc) noexcept
{
    crc = ~crc;
    for (const u8 b : data)
    {
        crc = kTable[(crc ^ b) & 0xFFu] ^ (crc >> 8);
    }
    return ~crc;
}
} // namespace g7
