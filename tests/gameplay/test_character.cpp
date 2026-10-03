// Characters (M8 part A): values from an Npc instance, inventory order, equipment slots and requirements,
// protection, experience and levels - with content kinds and items from an in-memory script.

#include <g7/gameplay/Character.hpp>
#include <g7/gameplay/ScriptContent.hpp>

#include <doctest/doctest.h>

#include <ostream> // doctest needs it to print std::string operands
#include <string>

using namespace g7;
using namespace g7::gameplay;

namespace
{
constexpr const char* kContent = R"(
Item "it_sword" { name = "Schwert", category = "melee_1h", requires = { str = 20 } }
Item "it_club" { name = "Knüppel", category = "melee_1h" }
Item "it_bow" { name = "Bogen", category = "bow", requires = { bow = 1 } }
Item "it_armor" { name = "Rüstung", category = "armor", protection = { edge = 15, blunt = 10 } }
Item "it_ring_a" { name = "Ring A", category = "ring", protection = { edge = 2 } }
Item "it_ring_b" { name = "Ring B", category = "ring", protection = { edge = 3 } }
Item "it_ring_c" { name = "Ring C", category = "ring" }
Item "it_apple" { name = "Apfel", category = "food" }
Item "it_potion" { name = "Trank", category = "potion" }
Item "it_scroll" { name = "Spruchrolle", category = "scroll" }
Npc "pc_hero" {
    name = "Held", level = 0,
    attributes = { hp_max = 40, str = 10, dex = 10 },
    talents = { picklock = 1 },
    protection = { fire = 5 },
    inventory = { it_apple = 3, it_potion = 1 },
    equipment = { "it_club", "it_armor" },
}
Npc "npc_bad" { name = "Kaputt", attributes = { luck = 3 } }
Item "it_odd" { name = "Seltsam", category = "weapon" }
)";

struct Fixture
{
    script::ScriptVm vm;
    ItemLookup items;

    Fixture()
    {
        script::ScriptConfig config;
        config.listFiles = [] { return std::vector<std::string>{"content.lua"}; };
        config.readFile = [](std::string_view) -> Result<std::string> { return std::string(kContent); };
        vm = script::ScriptVm::create(config).value();
        defineContentKinds(vm);
        vm.loadAll();
        items = [this](std::string_view name) -> std::optional<ItemInfo>
        {
            const script::Instance* i = vm.findInstance("Item", name);
            return i ? std::optional(itemInfo(*i)) : std::nullopt;
        };
    }

    Character hero() { return Character::fromInstance(*vm.findInstance("Npc", "pc_hero"), items).value(); }
};
} // namespace

TEST_CASE("Character: values from the Npc instance, the schema knows the categories")
{
    Fixture f;
    // Only it_odd's category is a problem (the bad attribute shows when the character is built).
    REQUIRE(f.vm.errors().size() == 1);
    CHECK(f.vm.errors()[0].message.find("field 'category' = \"weapon\" is none of melee_1h") !=
          std::string::npos);

    const Character hero = f.hero();
    CHECK(hero.name() == "Held");
    CHECK(hero.level() == 0);
    CHECK(hero.attribute("hp") == 40); // a maximum without a current value starts full
    CHECK(hero.attribute("str") == 10);
    CHECK(hero.attribute("luck") == 0);
    CHECK(hero.talent("picklock") == 1);
    CHECK(hero.talent("sneak") == 0);
    // Equipment from the instance, its protection counted.
    CHECK(hero.equipped(EquipSlot::Melee) == "it_club");
    CHECK(hero.equipped(EquipSlot::Armor) == "it_armor");
    CHECK(hero.protection("edge") == 15);
    CHECK(hero.protection("fire") == 5);

    auto bad = Character::fromInstance(*f.vm.findInstance("Npc", "npc_bad"), f.items);
    REQUIRE_FALSE(bad.ok());
    CHECK(bad.error().message.find("attributes: unknown 'luck'") != std::string::npos);
}

TEST_CASE("Character: inventory sorted by category, items in and out")
{
    Fixture f;
    Character hero = f.hero();
    hero.addItem("it_scroll");
    hero.addItem("it_bow");
    const auto inventory = hero.inventory(f.items);
    std::vector<std::string> order;
    for (const ItemStack& s : inventory)
    {
        order.push_back(s.item);
    }
    // Weapons first, then armour, scrolls, potions, food (kItemCategories).
    CHECK(order ==
          std::vector<std::string>{"it_club", "it_bow", "it_armor", "it_scroll", "it_potion", "it_apple"});
    CHECK(hero.itemCount("it_apple") == 3);
    CHECK_FALSE(hero.removeItem("it_apple", 4));
    CHECK(hero.itemCount("it_apple") == 3);
    CHECK(hero.removeItem("it_apple", 3));
    CHECK(hero.itemCount("it_apple") == 0);
    // Removing an equipped item takes it off.
    CHECK(hero.removeItem("it_armor"));
    CHECK(hero.equipped(EquipSlot::Armor).empty());
    CHECK(hero.protection("edge") == 0);
}

TEST_CASE("Character: equipment slots, requirements, rings and runes")
{
    Fixture f;
    Character hero = f.hero();
    // Not in the inventory; requirements (an attribute, a talent).
    CHECK_FALSE(hero.equip("it_sword", f.items).ok());
    hero.addItem("it_sword");
    const auto weak = hero.equip("it_sword", f.items);
    REQUIRE_FALSE(weak.ok());
    CHECK(weak.error().message == "\"it_sword\" needs str 20");
    REQUIRE(hero.setAttribute("str", 25).ok());
    CHECK(hero.equip("it_sword", f.items).value() == EquipSlot::Melee); // replaces the club
    CHECK(hero.equipped(EquipSlot::Melee) == "it_sword");
    hero.addItem("it_bow");
    CHECK_FALSE(hero.equip("it_bow", f.items).ok()); // needs the talent bow 1
    REQUIRE(hero.setTalent("bow", 1).ok());
    CHECK(hero.equip("it_bow", f.items).value() == EquipSlot::Ranged);
    // Not equippable.
    CHECK_FALSE(hero.equip("it_apple", f.items).ok());
    // Rings: two places, the third replaces the first one.
    for (const char* ring : {"it_ring_a", "it_ring_b", "it_ring_c"})
    {
        hero.addItem(ring);
    }
    CHECK(hero.equip("it_ring_a", f.items).value() == EquipSlot::Ring1);
    CHECK(hero.equip("it_ring_b", f.items).value() == EquipSlot::Ring2);
    CHECK(hero.protection("edge") == 15 + 2 + 3);
    CHECK(hero.equip("it_ring_c", f.items).value() == EquipSlot::Ring1);
    CHECK(hero.protection("edge") == 15 + 3);
    // The same ring twice needs two of them.
    CHECK_FALSE(hero.equip("it_ring_b", f.items).ok());
    hero.unequip(EquipSlot::Ring2);
    CHECK(hero.equipped(EquipSlot::Ring2).empty());
    // Scrolls fill the rune places one after another.
    hero.addItem("it_scroll", 2);
    CHECK(hero.equip("it_scroll", f.items).value() == EquipSlot::Rune1);
    const auto rune2 = static_cast<EquipSlot>(static_cast<usize>(EquipSlot::Rune1) + 1);
    CHECK(hero.equip("it_scroll", f.items).value() == rune2);
    CHECK(slotName(rune2) == "rune2");
    CHECK(slotFromName("ring2") == EquipSlot::Ring2);
    CHECK_FALSE(slotFromName("pocket").has_value());
}

TEST_CASE("Character: hp and mana within their maximum, experience and levels")
{
    Fixture f;
    Character hero = f.hero();
    REQUIRE(hero.setAttribute("hp", 100).ok());
    CHECK(hero.attribute("hp") == 40);
    REQUIRE(hero.setAttribute("hp", -5).ok());
    CHECK(hero.attribute("hp") == 0);
    REQUIRE(hero.setAttribute("hp", 30).ok());
    REQUIRE(hero.setAttribute("hp_max", 20).ok());
    CHECK(hero.attribute("hp") == 20);
    CHECK_FALSE(hero.setAttribute("charisma", 1).ok());
    CHECK_FALSE(hero.setTalent("swimming", 1).ok());

    // Gothic 1: level n needs 500 * n * (n + 1) / 2.
    const auto xpFor = [](i32 level) { return 500 * level * (level + 1) / 2; };
    CHECK(hero.addExperience(400, xpFor, 10) == 0);
    CHECK(hero.addExperience(100, xpFor, 10) == 1);  // 500: level 1
    CHECK(hero.addExperience(2600, xpFor, 10) == 2); // 3100: levels 2 and 3
    CHECK(hero.level() == 3);
    CHECK(hero.experience() == 3100);
    CHECK(hero.learnPoints() == 30);
}
