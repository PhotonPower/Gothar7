// The dungeon's chests and niches (game/scripts/mobs/dungeon.lua, welt W8): every mob_dng_ definition is a
// chest or a niche with contents, and every item in it exists.

#include <g7/runtime/Engine.hpp>
#include <g7/script/ScriptVm.hpp>

#include <doctest/doctest.h>

#include <ostream> // doctest needs it to print std::string operands
#include <string>

using namespace g7;

TEST_CASE("Dungeon mobs: chests and niches with existing items")
{
    EngineConfig config;
    config.headless = true;
    Engine engine(config);
    REQUIRE(engine.init().ok());
    const script::ScriptVm* scripts = engine.scripts();
    REQUIRE(scripts != nullptr);

    u32 definitions = 0;
    for (const script::Instance& instance : scripts->instances())
    {
        if (instance.kind != "Mob" || !instance.name.starts_with("mob_dng_"))
        {
            continue;
        }
        ++definitions;
        CAPTURE(instance.name);
        const std::string_view type = instance.fields["type"].asString();
        CHECK((type == "chest" || type == "niche"));
        const script::Table* contents = instance.fields["contents"].asTable();
        REQUIRE(contents != nullptr);
        CHECK_FALSE(contents->fields.empty());
        for (const auto& [item, count] : contents->fields)
        {
            CAPTURE(item);
            CHECK(scripts->findInstance("Item", item) != nullptr);
            CHECK(count.asInteger(0) >= 1);
        }
        if (!instance.fields["lock"].asString().empty())
        {
            CHECK(type == "chest"); // niches have no lid
        }
    }
    CHECK(definitions >= 11);
}
