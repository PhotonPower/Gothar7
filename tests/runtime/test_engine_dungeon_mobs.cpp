// The dungeon's chests and niches (game/scripts/mobs/dungeon.lua, welt W8): every mob_dng_ definition is a
// chest or a niche with contents, and every item in it exists.

#include <g7/runtime/Engine.hpp>
#include <g7/script/ScriptVm.hpp>

#include <doctest/doctest.h>

#include <algorithm>
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
    CHECK(definitions >= 50);
}

TEST_CASE("Dungeon mobs: the Sensenklinge lies in the hoard's niche, the best one-handed weapon, the hero "
          "wields it")
{
    EngineConfig config;
    config.headless = true;
    Engine engine(config);
    REQUIRE(engine.init().ok());
    const script::ScriptVm* scripts = engine.scripts();
    REQUIRE(scripts != nullptr);

    const script::Instance* blade = scripts->findInstance("Item", "it_scythe_blade");
    REQUIRE(blade != nullptr);
    CHECK(blade->fields["category"].asString() == "melee_1h");
    const script::Table* damage = blade->fields["damage"].asTable();
    REQUIRE(damage != nullptr);
    const i64 edge = damage->fields.at("edge").asInteger(0);
    // Clearly the best one-handed weapon: above every other melee_1h item.
    for (const script::Instance& instance : scripts->instances())
    {
        if (instance.kind == "Item" && instance.name != "it_scythe_blade" &&
            instance.fields["category"].asString() == "melee_1h")
        {
            CAPTURE(instance.name);
            const script::Table* other = instance.fields["damage"].asTable();
            i64 best = 0;
            if (other != nullptr)
            {
                for (const auto& [type, value] : other->fields)
                {
                    best = std::max(best, value.asInteger(0));
                }
            }
            CHECK(edge > best);
        }
    }

    const script::Instance* niche = scripts->findInstance("Mob", "mob_dng_e8_niche_hort");
    REQUIRE(niche != nullptr);
    const script::Table* contents = niche->fields["contents"].asTable();
    REQUIRE(contents != nullptr);
    CHECK(contents->fields.contains("it_scythe_blade"));
    CHECK(contents->fields.contains("it_brood_gland"));

    // The hero takes it up and wields it (moderate strength needed).
    REQUIRE(engine.runConsoleLine("set_stat('str', 20) give_item('it_scythe_blade')").ok());
    auto slot = engine.runConsoleLine("equip('it_scythe_blade')");
    REQUIRE_MESSAGE(slot.ok(), (slot.ok() ? "" : slot.error().message));
    CHECK(slot.value().asString() == "melee");
    auto equipped = engine.runConsoleLine("equipped('melee')");
    REQUIRE(equipped.ok());
    CHECK(equipped.value().asString() == "it_scythe_blade");
}

TEST_CASE("Dungeon mobs: level 1 holds two light scrolls in open containers (nobody goes down without light)")
{
    EngineConfig config;
    config.headless = true;
    Engine engine(config);
    REQUIRE(engine.init().ok());
    const script::ScriptVm* scripts = engine.scripts();
    REQUIRE(scripts != nullptr);
    i64 scrolls = 0;
    for (const char* name : {"mob_dng_e1_chest", "mob_dng_e1_niche"})
    {
        CAPTURE(name);
        const script::Instance* mob = scripts->findInstance("Mob", name);
        REQUIRE(mob != nullptr);
        CHECK(mob->fields["lock"].asString().empty()); // no lockpick needed
        const script::Table* contents = mob->fields["contents"].asTable();
        REQUIRE(contents != nullptr);
        if (const auto it = contents->fields.find("it_scroll_light"); it != contents->fields.end())
        {
            CHECK(it->second.asInteger(0) >= 1); // one in each
            scrolls += it->second.asInteger(0);
        }
        else
        {
            FAIL_CHECK("no light scroll");
        }
    }
    CHECK(scrolls >= 2);
}

TEST_CASE("Dungeon mobs: level 4's stair chest holds the fire rune and a one-hander, the two-hander lies on "
          "level 6")
{
    EngineConfig config;
    config.headless = true;
    Engine engine(config);
    REQUIRE(engine.init().ok());
    const script::ScriptVm* scripts = engine.scripts();
    REQUIRE(scripts != nullptr);
    const auto contents = [&](std::string_view mob) -> const script::Table&
    {
        const script::Instance* m = scripts->findInstance("Mob", mob);
        REQUIRE(m != nullptr);
        const script::Table* t = m->fields["contents"].asTable();
        REQUIRE(t != nullptr);
        return *t;
    };
    const script::Table& stairs = contents("mob_dng_e4_chest_treppe");
    CHECK(stairs.fields.contains("it_rune_firebolt"));
    CHECK_FALSE(stairs.fields.contains("it_sword_2h"));
    bool oneHander = false;
    for (const auto& [item, count] : stairs.fields)
    {
        const script::Instance* def = scripts->findInstance("Item", item);
        oneHander = oneHander || (def != nullptr && def->fields["category"].asString() == "melee_1h");
    }
    CHECK(oneHander);
    CHECK(contents("mob_dng_e6_chest_hebel").fields.contains("it_sword_2h"));
}
