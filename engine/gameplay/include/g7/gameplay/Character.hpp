#pragma once

// Characters (M8 part A): attributes, talents, protection, experience and level, inventory and equipment -
// for the hero and later every NPC (the hero is an Npc instance in Lua, `pc_hero`). Values and formulas come
// from the scripts (docs/modules/gameplay.md "Charakter", "Items & Inventar"); the rules are Gothic 1's.

#include <g7/core/Result.hpp>
#include <g7/core/Types.hpp>
#include <g7/script/ScriptVm.hpp>

#include <array>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace g7::gameplay
{
/// Item categories (Item.category), in the order the inventory lists them.
inline constexpr std::array<std::string_view, 18> kItemCategories = {
    "melee_1h", "melee_2h", "bow",    "crossbow", "ammo", "armor",    "helmet", "ring",  "amulet",
    "belt",     "rune",     "scroll", "potion",   "food", "document", "key",    "torch", "misc"};
/// Attributes a character has (Npc.attributes); hp_max/mana_max bound hp/mana.
inline constexpr std::array<std::string_view, 6> kAttributes = {"hp",       "hp_max", "mana",
                                                                "mana_max", "str",    "dex"};
/// Talents (Npc.talents), each with a level (0 = not learnt).
inline constexpr std::array<std::string_view, 9> kTalents = {"melee_1h",   "melee_2h",   "bow",
                                                             "crossbow",   "sneak",      "picklock",
                                                             "pickpocket", "acrobatics", "magic_circle"};
/// Damage types (Item.damage, Item.protection, Npc.protection).
inline constexpr std::array<std::string_view, 6> kDamageTypes = {"edge", "blunt", "point",
                                                                 "fire", "magic", "fall"};

enum class EquipSlot : u8
{
    Melee,
    Ranged,
    Armor,
    Helmet,
    Ring1,
    Ring2,
    Amulet,
    Belt,
    Rune1, ///< seven rune/scroll places (Rune1..Rune7)
    Count = Rune1 + 7,
};
[[nodiscard]] std::string_view slotName(EquipSlot slot) noexcept; ///< "melee", "ring1", "rune3" ...
[[nodiscard]] std::optional<EquipSlot> slotFromName(std::string_view name) noexcept;

/// What the character code needs of an Item instance.
struct ItemInfo
{
    std::string name;     ///< display name
    std::string category; ///< one of kItemCategories
    std::map<std::string, i32, std::less<>>
        requirements; ///< Item.requires: attributes or talents ("str" = 10)
    std::map<std::string, i32, std::less<>> protection;
    bool stackable = true; ///< false for weapons, armour, jewellery: each one its own entry
};
/// Item lookup by instance name (from the scripts); nullopt for unknown items.
using ItemLookup = std::function<std::optional<ItemInfo>(std::string_view item)>;
/// ItemInfo of a script instance (Item).
[[nodiscard]] ItemInfo itemInfo(const script::Instance& item);

struct ItemStack
{
    std::string item; ///< instance name
    u32 count = 0;
};

class Character
{
public:
    /// Values of an Npc instance: attributes, talents, protection, guild, level, inventory (`inventory = {
    /// it_x = 3 }` plus every `equipment` entry), equipment (`equipment` is equipped where the rules allow).
    [[nodiscard]] static Result<Character> fromInstance(const script::Instance& npc, const ItemLookup& items);

    [[nodiscard]] const std::string& instance() const noexcept { return m_instance; }
    [[nodiscard]] const std::string& name() const noexcept { return m_name; }
    [[nodiscard]] const std::string& guild() const noexcept { return m_guild; }

    // Attributes and talents (unknown names: 0, setting them is an error).
    [[nodiscard]] i32 attribute(std::string_view name) const noexcept;
    Result<void> setAttribute(std::string_view name, i32 value); ///< hp/mana clamped to 0..max
    [[nodiscard]] i32 talent(std::string_view name) const noexcept;
    Result<void> setTalent(std::string_view name, i32 level);
    /// Own protection plus what the equipped items give.
    [[nodiscard]] i32 protection(std::string_view damageType) const noexcept;

    // Experience: `xpForLevel(level)` is the total experience level `level` needs (scripts: Progression).
    [[nodiscard]] i32 level() const noexcept { return m_level; }
    [[nodiscard]] i32 experience() const noexcept { return m_xp; }
    [[nodiscard]] i32 learnPoints() const noexcept { return m_learnPoints; }
    void setLearnPoints(i32 points) noexcept { m_learnPoints = points; }
    /// Adds experience; returns the number of levels gained (each gives `learnPointsPerLevel`).
    u32 addExperience(i32 xp, const std::function<i32(i32 level)>& xpForLevel, i32 learnPointsPerLevel);

    // Inventory: stacks in category order (kItemCategories), then by name; no weight limit (Gothic).
    void addItem(std::string_view item, u32 count = 1);
    /// False (and nothing removed) if there are fewer than `count`; equipped ones are unequipped first.
    bool removeItem(std::string_view item, u32 count = 1);
    [[nodiscard]] u32 itemCount(std::string_view item) const noexcept;
    [[nodiscard]] std::vector<ItemStack> inventory(const ItemLookup& items) const;

    // Equipment: the slot follows the category (rings: the free one of two, runes/scrolls: the first free
    // one).
    /// Errors: not in the inventory, no slot for its category, requirements not met ("needs str 30").
    Result<EquipSlot> equip(std::string_view item, const ItemLookup& items);
    void unequip(EquipSlot slot) noexcept;
    [[nodiscard]] const std::string& equipped(EquipSlot slot) const noexcept; ///< empty: nothing
    /// The slot an item of this category goes to (first candidate), nullopt if it cannot be equipped.
    [[nodiscard]] static std::optional<EquipSlot> slotFor(std::string_view category) noexcept;

private:
    [[nodiscard]] i32 requirementValue(std::string_view name) const noexcept;

    std::string m_instance;
    std::string m_name;
    std::string m_guild;
    std::map<std::string, i32, std::less<>> m_attributes;
    std::map<std::string, i32, std::less<>> m_talents;
    std::map<std::string, i32, std::less<>> m_protection;
    i32 m_level = 0;
    i32 m_xp = 0;
    i32 m_learnPoints = 0;
    std::map<std::string, u32, std::less<>> m_items;
    std::array<std::string, static_cast<usize>(EquipSlot::Count)> m_equipped;
    std::array<std::map<std::string, i32, std::less<>>, static_cast<usize>(EquipSlot::Count)>
        m_equippedProtection;
};
} // namespace g7::gameplay
