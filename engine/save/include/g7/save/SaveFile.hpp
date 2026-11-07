#pragma once

// The save file .g7save (M15 part A, ADR 0021, docs/modules/save.md "Dateiformat"): a binary chunk container
// with a CRC-32 per chunk; the chunks hold JSON (zstd-packed) and the thumbnail PNG.
//
//   header (20 bytes): "G7SV" | format u16 | flags u16 | chunk count u32 | reserved u32 | header crc u32
//   chunk  (20 bytes): tag u32 | version u16 | flags u16 (bit 0: zstd) | raw size u32 | size u32 | crc u32 |
//   data
//                      (the crc covers the first 16 bytes of the chunk head and the data)
//   chunks: META (meta data, JSON, first, not packed), THMB (PNG, not packed), GLOB (the game state without
//   its worlds), WRLD (one per visited world). Unknown chunks are skipped (logged).

#include <g7/core/FileSystem.hpp>
#include <g7/core/Result.hpp>
#include <g7/core/Types.hpp>
#include <g7/save/GameState.hpp>

#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace g7::save
{
inline constexpr u16 kSaveFormat = 1;

/// What the list of save slots shows without reading the whole file.
struct SaveMeta
{
    std::string name;        ///< "Leonberg - Kapitel 2 - Tag 3, 14:20" (S4)
    std::string world;       ///< VFS path of the current world
    std::string place;       ///< where the hero was (world or place name)
    std::string language;    ///< UI language when saved
    std::string gameVersion; ///< "0.1.0"
    i64 savedAtUtc = 0;      ///< seconds since 1970
    f64 playSeconds = 0.0;   ///< real time played
    u32 day = 0;             ///< game day (1 = the first)
    u32 minute = 0;          ///< minute of the day
    u32 chapter = 1;
    i32 heroLevel = 0;
    u64 contentStamp = 0; ///< hash of the visited world files: another content warns on loading (S8)
    friend bool operator==(const SaveMeta&, const SaveMeta&) = default;
};

struct SaveFile
{
    SaveMeta meta;
    std::vector<u8> thumbnailPng; ///< empty: none
    GameState state;
};

/// The bytes of a save file.
[[nodiscard]] Result<std::vector<u8>> encodeSave(const SaveFile& save);
/// Reads and checks a whole save file (every CRC, then the JSON); nothing is half-read.
[[nodiscard]] Result<SaveFile> decodeSave(std::span<const u8> bytes);
/// Only the meta data (and the thumbnail): reads the first chunks, not the game state.
[[nodiscard]] Result<SaveMeta> decodeMeta(std::span<const u8> bytes, std::vector<u8>* thumbnail = nullptr);

/// Writes atomically (a temporary file, then renamed): the old save stays valid until the new one is
/// complete.
[[nodiscard]] Result<void> writeSaveFile(const fs::Path& path, const SaveFile& save);
[[nodiscard]] Result<SaveFile> readSaveFile(const fs::Path& path);
[[nodiscard]] Result<SaveMeta> readSaveMeta(const fs::Path& path, std::vector<u8>* thumbnail = nullptr);

/// A slot of the save directory: slot_01 ... slot_15, quick, auto (S2).
struct SlotInfo
{
    std::string slot;             ///< "slot_03", "quick", "auto"
    fs::Path path;                ///< <dir>/<slot>.g7save
    std::optional<SaveMeta> meta; ///< empty: no file, or it cannot be read
    std::string error;            ///< why it cannot be read ("beschädigt", "neuere Version")
    std::vector<u8> thumbnailPng;
};
inline constexpr u32 kSaveSlots = 15;
/// slot_01 .. slot_<kSaveSlots>, then quick and auto; each with what its file says.
[[nodiscard]] std::vector<SlotInfo> listSlots(const fs::Path& directory);
[[nodiscard]] fs::Path slotPath(const fs::Path& directory, std::string_view slot);
} // namespace g7::save
