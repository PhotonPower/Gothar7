#pragma once

// CRC-32 (IEEE 802.3, the one zip and PNG use): checksums of save game chunks (M15, ADR 0021).

#include <g7/core/Types.hpp>

#include <span>

namespace g7
{
/// CRC-32 of `data`; pass the previous result as `crc` to continue over several pieces.
[[nodiscard]] u32 crc32(std::span<const u8> data, u32 crc = 0) noexcept;
} // namespace g7
