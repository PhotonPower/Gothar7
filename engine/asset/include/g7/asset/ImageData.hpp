#pragma once

#include <g7/core/FileSystem.hpp>
#include <g7/core/Result.hpp>
#include <g7/core/Types.hpp>

#include <span>
#include <string_view>
#include <vector>

namespace g7::asset
{
/// Decoded image, always 8-bit RGBA, rows top to bottom (first row = top, as in the file and in
/// glTF's UV convention: v = 0 is the top edge).
struct ImageData
{
    u32 width = 0;
    u32 height = 0;
    std::vector<u8> rgba8;
};

/// Decodes PNG, JPEG, TGA or BMP (ADR 0014). Grey/RGB sources are expanded to RGBA.
[[nodiscard]] Result<ImageData> decodeImage(std::span<const u8> bytes,
                                            std::string_view debugName = "<memory>");
[[nodiscard]] Result<ImageData> loadImage(const fs::Path& path);

/// PNG encoding of `image` (rows top to bottom); for screenshots and tools.
[[nodiscard]] Result<std::vector<u8>> encodePng(const ImageData& image);
[[nodiscard]] Result<void> savePng(const fs::Path& path, const ImageData& image);
} // namespace g7::asset
