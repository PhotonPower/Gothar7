// The save file .g7save (M15 part A, ADR 0021): a game state with two worlds and a thumbnail round-trips, the
// meta data reads without the game state, every damaged byte and a cut-off file are refused, unknown chunks
// are skipped, newer formats refused, slots listed with their meta data; the file is replaced atomically.

#include <g7/core/Crc32.hpp>
#include <g7/save/SaveFile.hpp>

#include <doctest/doctest.h>

#include <filesystem>
#include <ostream> // doctest needs it to print std::string operands
#include <string>

using namespace g7;

namespace
{
save::SaveFile sample()
{
    save::SaveFile f;
    f.meta.name = "Lager - Kapitel 1 - Tag 1, 12:00";
    f.meta.world = "testworld/camp.g7world";
    f.meta.place = "Lager";
    f.meta.language = "de";
    f.meta.gameVersion = "0.1.0";
    f.meta.savedAtUtc = 1'790'000'000;
    f.meta.playSeconds = 1234.5;
    f.meta.day = 1;
    f.meta.minute = 720;
    f.meta.chapter = 1;
    f.meta.heroLevel = 2;
    f.meta.contentStamp = 0x1234'5678'9abc'def0ull;
    f.thumbnailPng = {0x89, 'P', 'N', 'G', 1, 2, 3, 4};
    f.state.clock = 720.5;
    f.state.story = script::makeTable({}, {{"chapter", script::Value(i64{1})}});
    f.state.currentWorld = "testworld/camp.g7world";
    for (const char* path : {"testworld/cave.g7world", "testworld/camp.g7world"})
    {
        save::WorldState w;
        w.path = path;
        w.spentTriggers = {3};
        w.mobs.push_back({7, "mob_camp_chest", false, true, {{"it_gulden", 10}}});
        save::NpcState npc;
        npc.name = "npc_old_man";
        npc.instance = "npc_old_man";
        npc.position = Vec3(1.0f, 0.0f, 2.0f);
        w.npcs.push_back(npc);
        f.state.worlds.push_back(std::move(w));
    }
    return f;
}
} // namespace

TEST_CASE("CRC-32: the standard check value and continuing over pieces")
{
    const std::string text = "123456789";
    const std::span<const u8> bytes(reinterpret_cast<const u8*>(text.data()), text.size());
    CHECK(crc32(bytes) == 0xCBF43926u);
    CHECK(crc32(bytes.subspan(4), crc32(bytes.first(4))) == 0xCBF43926u);
    CHECK(crc32({}) == 0u);
}

TEST_CASE("Save file: round trip, meta data alone, the same bytes again")
{
    const save::SaveFile f = sample();
    auto bytes = save::encodeSave(f);
    REQUIRE(bytes.ok());
    CHECK(bytes.value()[0] == 'G');
    CHECK(bytes.value()[3] == 'V');
    auto back = save::decodeSave(bytes.value());
    REQUIRE_MESSAGE(back.ok(), (back.ok() ? "" : back.error().message));
    CHECK(back.value().meta == f.meta);
    CHECK(back.value().thumbnailPng == f.thumbnailPng);
    CHECK(save::toJson(back.value().state) == save::toJson(f.state));
    auto again = save::encodeSave(back.value());
    REQUIRE(again.ok());
    CHECK(again.value() == bytes.value());

    std::vector<u8> thumbnail;
    auto meta = save::decodeMeta(bytes.value(), &thumbnail);
    REQUIRE(meta.ok());
    CHECK(meta.value() == f.meta);
    CHECK(thumbnail == f.thumbnailPng);
}

TEST_CASE("Save file: every damaged byte and a cut-off file are refused")
{
    auto bytes = save::encodeSave(sample());
    REQUIRE(bytes.ok());
    const std::vector<u8>& good = bytes.value();
    usize refused = 0;
    for (usize i = 0; i < good.size(); ++i)
    {
        std::vector<u8> bad = good;
        bad[i] ^= 0x5A;
        refused += save::decodeSave(bad).ok() ? 0u : 1u;
    }
    CHECK(refused == good.size()); // the header CRC and the chunk CRCs cover every byte
    for (const usize keep : {usize{0}, usize{10}, usize{30}, good.size() / 2, good.size() - 1})
    {
        CHECK_FALSE(save::decodeSave(std::span(good).first(keep)).ok());
    }
    CHECK_FALSE(save::decodeSave(std::vector<u8>{'P', 'K', 3, 4}).ok());
}

TEST_CASE("Save file: unknown chunks are skipped, newer formats and chunk versions refused")
{
    auto bytes = save::encodeSave(sample());
    REQUIRE(bytes.ok());
    std::vector<u8> file = bytes.value();
    // A chunk "XTRA" appended (as a later version might): the chunk count +1, the header CRC anew.
    const std::vector<u8> payload = {1, 2, 3};
    const std::vector<u8> head = {'X', 'T', 'R', 'A', 1, 0, 0, 0, 3, 0, 0, 0, 3, 0, 0, 0};
    const u32 crc = crc32(payload, crc32(head));
    file.insert(file.end(), head.begin(), head.end());
    for (int i = 0; i < 4; ++i)
    {
        file.push_back(static_cast<u8>(crc >> (8 * i)));
    }
    file.insert(file.end(), payload.begin(), payload.end());
    file[8] = static_cast<u8>(file[8] + 1);
    const auto rewriteHeaderCrc = [](std::vector<u8>& f)
    {
        const u32 h = crc32(std::span<const u8>(f.data(), 16));
        for (int i = 0; i < 4; ++i)
        {
            f[16 + static_cast<usize>(i)] = static_cast<u8>(h >> (8 * i));
        }
    };
    rewriteHeaderCrc(file);
    auto withExtra = save::decodeSave(file);
    REQUIRE_MESSAGE(withExtra.ok(), (withExtra.ok() ? "" : withExtra.error().message));
    CHECK(withExtra.value().state.worlds.size() == 2);

    std::vector<u8> newer = bytes.value();
    newer[4] = 9; // format 9
    rewriteHeaderCrc(newer);
    auto refused = save::decodeSave(newer);
    REQUIRE_FALSE(refused.ok());
    CHECK(refused.error().message.find("newer") != std::string::npos);
}

TEST_CASE("Save file: written atomically, slots listed with their meta data")
{
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "g7_save_slots_test";
    std::filesystem::remove_all(dir);
    save::SaveFile f = sample();
    REQUIRE(save::writeSaveFile(save::slotPath(dir, "slot_03"), f).ok());
    f.meta.name = "zweiter Stand";
    REQUIRE(save::writeSaveFile(save::slotPath(dir, "slot_03"), f).ok()); // replaces it
    CHECK_FALSE(std::filesystem::exists(save::slotPath(dir, "slot_03").string() + ".tmp"));
    REQUIRE(fs::writeText(save::slotPath(dir, "quick"), "kaputt").ok());

    const auto slots = save::listSlots(dir);
    REQUIRE(slots.size() == save::kSaveSlots + 2);
    CHECK(slots[0].slot == "slot_01");
    CHECK_FALSE(slots[0].meta.has_value());
    CHECK(slots[0].error.empty()); // empty, not broken
    REQUIRE(slots[2].meta.has_value());
    CHECK(slots[2].meta->name == "zweiter Stand");
    CHECK(slots[2].thumbnailPng == f.thumbnailPng);
    CHECK(slots[save::kSaveSlots].slot == "quick");
    CHECK_FALSE(slots[save::kSaveSlots].meta.has_value());
    CHECK_FALSE(slots[save::kSaveSlots].error.empty());
    CHECK(slots[save::kSaveSlots + 1].slot == "auto");

    auto read = save::readSaveFile(save::slotPath(dir, "slot_03"));
    REQUIRE(read.ok());
    CHECK(read.value().state.worlds.size() == 2);
    std::filesystem::remove_all(dir);
}
