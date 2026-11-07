// Leonberg lebt (label "headless"; its own ctest entry for its run time): the residents' routines name places
// of welt's waynet (checked against the versioned world file, also in CI); and, where the generated town is
// there (locally, not in CI: generated files are not versioned), the day in Leonberg with each resident in
// the state of his routine at the right place; the night: test_engine_leonberg_residents_night.cpp.

#include "LeonbergResidents.hpp"

#include <g7/ai/Waynet.hpp>
#include <g7/core/Config.hpp>
#include <g7/runtime/Engine.hpp>
#include <g7/world/WorldFile.hpp>

#include <doctest/doctest.h>

#include <filesystem>
#include <format>
#include <fstream>
#include <ostream> // doctest needs it to print std::string operands
#include <sstream>
#include <string>

using namespace g7;

namespace
{
const std::string kWorld = std::string(G7_ASSET_SOURCE_DIR) + "/worlds/leonberg/leonberg.g7world";
const std::string kGenerated =
    std::string(G7_ASSET_SOURCE_DIR) + "/worlds/leonberg/generated/leonberg_terrain.r16";

} // namespace

TEST_CASE("Leonberg residents: every routine place is in Leonberg's waynet, every state exists")
{
    std::ifstream in(kWorld, std::ios::binary);
    REQUIRE(in.good());
    std::stringstream text;
    text << in.rdbuf();
    auto world = world::parseWorldFile(text.str(), "leonberg.g7world");
    REQUIRE_MESSAGE(world.ok(), (world.ok() ? "" : world.error().message));
    REQUIRE(world.value().waynet.has_value());
    const ai::Waynet waynet = ai::Waynet::build(*world.value().waynet);

    // The scripts (any world: Leonberg's geometry is not needed for this).
    EngineConfig config;
    config.appName = "leonberg";
    config.headless = true;
    config.world = fs::fromUtf8("testworld/camp.g7world");
    Engine engine(std::move(config));
    REQUIRE(engine.init().ok());
    usize entries = 0;
    for (const char* npc : test::kResidents)
    {
        const script::Instance* def = engine.scripts()->findInstance("Npc", npc);
        REQUIRE_MESSAGE(def != nullptr, npc);
        const script::Instance* routine =
            engine.scripts()->findInstance("Routine", def->fields["routine"].asString());
        REQUIRE_MESSAGE(routine != nullptr, npc);
        for (const script::Value& e : routine->fields.asTable()->array)
        {
            const std::string at(e["at"].asString());
            CHECK_MESSAGE((waynet.find(at) || waynet.findFreepoint(at)), routine->name, ": ", at);
            CHECK_MESSAGE(engine.scripts()->findInstance("State", e["state"].asString()) != nullptr,
                          routine->name, ": ", e["state"].asString());
            ++entries;
        }
    }
    CHECK(entries >= 95);
}

TEST_CASE("Leonberg residents: the day in the town (06:30-18:30), each at the place of his routine (with the "
          "generated town)")
{
    if (!std::filesystem::exists(kGenerated))
    {
        MESSAGE("skipped: Leonberg's generated files are not here (not versioned; welt's gothar-worldgen "
                "makes them)");
        return;
    }
    test::playLeonbergHours("06:30", 12);
}
