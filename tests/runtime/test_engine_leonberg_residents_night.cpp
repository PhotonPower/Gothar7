// Leonberg's residents at night (label "headless", with the generated town; not in CI): 18:30 to 06:30, each
// at the place of his routine every game hour. Its own ctest entry (runtime_leonberg_night) for its run time;
// the day is in test_engine_leonberg_residents.cpp.

#include "LeonbergResidents.hpp"

#include <doctest/doctest.h>

#include <filesystem>
#include <string>

using namespace g7;

TEST_CASE(
    "Leonberg residents: the night in the town (18:30-06:30), each at the place of his routine (with the "
    "generated town)")
{
    const std::string generated =
        std::string(G7_ASSET_SOURCE_DIR) + "/worlds/leonberg/generated/leonberg_terrain.r16";
    if (!std::filesystem::exists(generated))
    {
        MESSAGE("skipped: Leonberg's generated files are not here");
        return;
    }
    test::playLeonbergHours("18:30", 12);
}
