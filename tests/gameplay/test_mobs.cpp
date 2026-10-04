// Mobs (M8 part C): mobs.toml (the contract file, also the real one), slots in the world, lockpicking.

#include <g7/gameplay/Mobs.hpp>

#include <doctest/doctest.h>

#include <fstream>
#include <ostream> // doctest needs it to print std::string operands
#include <sstream>
#include <string>

using namespace g7;
using namespace g7::gameplay;

namespace
{
constexpr const char* kMobs = R"(
version = 1
[mobs.chest]
clips = "mob/chest"
enter = "t_open"
loop = "s_open"
leave = "t_close"
extra = { picklock = "s_picklock" }
[[mobs.chest.slots]]
name = "front"
pos = [0.0, 0.0, 0.65]
facing = [0.0, 0.0, -1.0]

[mobs.door]
clips = "mob/door"
enter = "t_open"
loop = ""
leave = ""
[[mobs.door.slots]]
name = "front"
pos = [0.5, 0.0, 0.6]
facing = [0.0, 0.0, -1.0]
[[mobs.door.slots]]
name = "back"
pos = [0.5, 0.0, -0.6]
facing = [0.0, 0.0, 1.0]
)";
} // namespace

TEST_CASE("Mobs: mobs.toml types, clips and slots")
{
    const MobTypes mobs = MobTypes::parse(kMobs, "mobs.toml").value();
    const MobType* chest = mobs.find("chest");
    REQUIRE(chest != nullptr);
    CHECK(chest->clip(chest->enter) == "mob/chest/t_open");
    CHECK(chest->clip(chest->extra.at("picklock")) == "mob/chest/s_picklock");
    REQUIRE(chest->slots.size() == 1);
    CHECK(chest->slots[0].position.z == doctest::Approx(0.65f));
    const MobType* door = mobs.find("door");
    REQUIRE(door != nullptr);
    CHECK(door->clip(door->loop).empty());
    CHECK(door->slots.size() == 2);
    CHECK(mobs.find("throne") == nullptr);

    CHECK_FALSE(MobTypes::parse("version = 2\n", "mobs.toml").ok());
    const auto bad =
        MobTypes::parse("version = 1\n[mobs.bed]\n[[mobs.bed.slots]]\nname = \"side\"\n", "mobs.toml");
    REQUIRE_FALSE(bad.ok());
    CHECK(bad.error().message == "mobs.toml: mobs.bed.slots[0]: needs 'pos' as three numbers");
    const auto flat = MobTypes::parse(
        "version = 1\n[mobs.bed]\n[[mobs.bed.slots]]\npos = [0,0,1]\nfacing = [0,1,0]\n", "mobs.toml");
    REQUIRE_FALSE(flat.ok());
    CHECK(flat.error().message.find("'facing' must point somewhere horizontally") != std::string::npos);
}

TEST_CASE("Mobs: the contract file in the repository parses")
{
    std::ifstream file(G7_ASSET_SOURCE_DIR "/data/mobs.toml");
    REQUIRE(file.good());
    std::stringstream text;
    text << file.rdbuf();
    const auto mobs = MobTypes::parse(text.str(), "mobs.toml");
    REQUIRE_MESSAGE(mobs.ok(), (mobs.ok() ? "" : mobs.error().message));
    for (const char* type : {"chest", "anvil", "bed", "door"})
    {
        REQUIRE_MESSAGE(mobs.value().find(type) != nullptr, type);
        CHECK_FALSE(mobs.value().find(type)->slots.empty());
    }
}

TEST_CASE("Mobs: slots in the world, the nearest free one")
{
    const MobTypes mobs = MobTypes::parse(kMobs, "mobs.toml").value();
    const MobType& door = *mobs.find("door");
    // A door at (10, 0, 0) turned 90 degrees left: its front (+Z) faces +X... (rotation about +Y by +90: +Z
    // -> +X)
    const Mat4 world =
        glm::translate(Mat4(1.0f), Vec3(10, 0, 0)) * glm::rotate(Mat4(1.0f), 1.5707963f, Vec3(0, 1, 0));
    const SlotPlace front = placeSlot(door, 0, world);
    CHECK(front.feet.x == doctest::Approx(10.6f));
    CHECK(front.feet.z == doctest::Approx(-0.5f));
    // It looks back at the door (towards -X): yaw of -X is +90 degrees (left turn from -Z).
    CHECK(front.yaw == doctest::Approx(1.5707963f));
    // From the back side the back slot is nearer; busy slots are skipped.
    CHECK(chooseSlot(door, world, Vec3(8, 0, 0))->index == 1);
    CHECK(chooseSlot(door, world, Vec3(12, 0, 0))->index == 0);
    CHECK(chooseSlot(door, world, Vec3(12, 0, 0), 0b01)->index == 1);
    CHECK_FALSE(chooseSlot(door, world, Vec3(12, 0, 0), 0b11).has_value());
    CHECK(yawOf(Vec3(0, 0, -1)) == doctest::Approx(0.0f));
}

TEST_CASE("Mobs: lockpicking - right steps open, a wrong one resets and may break the pick")
{
    CHECK(Lockpick::validCombination("LRRL"));
    CHECK_FALSE(Lockpick::validCombination(""));
    CHECK_FALSE(Lockpick::validCombination("LRX"));

    Lockpick lock("LRR");
    CHECK(lock.turn('L', 0.9f, 0.5f) == Lockpick::Result::Progress);
    CHECK(lock.turn('R', 0.9f, 0.5f) == Lockpick::Result::Progress);
    CHECK(lock.progress() == 2);
    // Wrong: back to the start; the roll decides about the pick.
    CHECK(lock.turn('L', 0.9f, 0.5f) == Lockpick::Result::Reset);
    CHECK(lock.progress() == 0);
    CHECK(lock.turn('R', 0.1f, 0.5f) == Lockpick::Result::Broken);
    CHECK(lock.progress() == 0);
    // With talent 2 (5 %) a roll of 0.1 holds.
    CHECK(lock.turn('R', 0.1f, 0.05f) == Lockpick::Result::Reset);
    for (const char c : std::string("LRR"))
    {
        lock.turn(c, 0.0f, 1.0f);
    }
    CHECK(lock.open());
    CHECK(lock.turn('L', 0.0f, 1.0f) == Lockpick::Result::Opened); // stays open
}
