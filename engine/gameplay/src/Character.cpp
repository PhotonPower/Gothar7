#include <g7/gameplay/Character.hpp>

#include <algorithm>
#include <format>

namespace g7::gameplay
{
namespace
{
template <usize N>
bool known(const std::array<std::string_view, N>& names, std::string_view name)
{
    return std::find(names.begin(), names.end(), name) != names.end();
}

usize categoryRank(std::string_view category)
{
    const auto it = std::find(kItemCategories.begin(), kItemCategories.end(), category);
    return static_cast<usize>(it - kItemCategories.begin());
}

/// Numbers of a script table with names from `names` ("attributes", "talents" ...).
template <usize N>
Result<std::map<std::string, i32, std::less<>>>
numberTable(const script::Value& table, const std::array<std::string_view, N>& names, std::string_view what)
{
    std::map<std::string, i32, std::less<>> out;
    const script::Table* t = table.asTable();
    if (t == nullptr)
    {
        return out;
    }
    for (const auto& [name, value] : t->fields)
    {
        if (!known(names, name))
        {
            std::string list;
            for (const std::string_view n : names)
            {
                list += (list.empty() ? "" : ", ") + std::string(n);
            }
            return Error{std::format("{}: unknown '{}' (known: {})", what, name, list)};
        }
        if (!value.isNumber())
        {
            return Error{std::format("{}.{} must be a number", what, name)};
        }
        out[name] = static_cast<i32>(value.asInteger());
    }
    return out;
}
} // namespace

std::string_view slotName(EquipSlot slot) noexcept
{
    constexpr std::array<std::string_view, static_cast<usize>(EquipSlot::Count)> kNames = {
        "melee", "ranged", "armor", "helmet", "ring1", "ring2", "amulet", "belt",
        "rune1", "rune2",  "rune3", "rune4",  "rune5", "rune6", "rune7"};
    return kNames[static_cast<usize>(slot)];
}

std::optional<EquipSlot> slotFromName(std::string_view name) noexcept
{
    for (usize i = 0; i < static_cast<usize>(EquipSlot::Count); ++i)
    {
        if (slotName(static_cast<EquipSlot>(i)) == name)
        {
            return static_cast<EquipSlot>(i);
        }
    }
    return std::nullopt;
}

ItemInfo itemInfo(const script::Instance& item)
{
    ItemInfo info;
    info.name = std::string(item.fields["name"].asString());
    info.category =
        item.fields["category"].isString() ? std::string(item.fields["category"].asString()) : "misc";
    for (const auto& [field, target] :
         {std::pair{"requires", &info.requirements}, std::pair{"protection", &info.protection}})
    {
        if (const script::Table* t = item.fields[field].asTable())
        {
            for (const auto& [name, value] : t->fields)
            {
                (*target)[name] = static_cast<i32>(value.asInteger());
            }
        }
    }
    info.stackable = !Character::slotFor(info.category) || info.category == "ammo" ||
                     info.category == "rune" || info.category == "scroll";
    return info;
}

std::optional<EquipSlot> Character::slotFor(std::string_view category) noexcept
{
    if (category == "melee_1h" || category == "melee_2h")
    {
        return EquipSlot::Melee;
    }
    if (category == "bow" || category == "crossbow")
    {
        return EquipSlot::Ranged;
    }
    if (category == "armor")
    {
        return EquipSlot::Armor;
    }
    if (category == "helmet")
    {
        return EquipSlot::Helmet;
    }
    if (category == "ring")
    {
        return EquipSlot::Ring1;
    }
    if (category == "amulet")
    {
        return EquipSlot::Amulet;
    }
    if (category == "belt")
    {
        return EquipSlot::Belt;
    }
    if (category == "rune" || category == "scroll")
    {
        return EquipSlot::Rune1;
    }
    return std::nullopt;
}

Result<Character> Character::fromInstance(const script::Instance& npc, const ItemLookup& items)
{
    Character c;
    c.m_instance = npc.name;
    c.m_name = std::string(npc.fields["name"].asString());
    c.m_guild = std::string(npc.fields["guild"].asString());
    c.m_level = static_cast<i32>(npc.fields["level"].asInteger(0));
    c.m_xp = static_cast<i32>(npc.fields["xp"].asInteger(0));
    c.m_learnPoints = static_cast<i32>(npc.fields["learn_points"].asInteger(0));
    const auto where = [&](std::string_view what)
    { return std::format("{} \"{}\": {}", npc.kind, npc.name, what); };
    auto attributes = numberTable(npc.fields["attributes"], kAttributes, where("attributes"));
    auto talents = numberTable(npc.fields["talents"], kTalents, where("talents"));
    auto protection = numberTable(npc.fields["protection"], kDamageTypes, where("protection"));
    if (!attributes || !talents || !protection)
    {
        return !attributes ? attributes.error() : !talents ? talents.error() : protection.error();
    }
    c.m_attributes = std::move(attributes).value();
    c.m_talents = std::move(talents).value();
    c.m_protection = std::move(protection).value();
    // Gothic: hp/mana without a maximum start full; a maximum without a current value starts at the maximum.
    for (const auto& [value, max] : {std::pair{"hp", "hp_max"}, std::pair{"mana", "mana_max"}})
    {
        if (!c.m_attributes.contains(max) && c.m_attributes.contains(value))
        {
            c.m_attributes[max] = c.m_attributes[value];
        }
        if (!c.m_attributes.contains(value) && c.m_attributes.contains(max))
        {
            c.m_attributes[value] = c.m_attributes[max];
        }
    }
    if (const script::Table* inventory = npc.fields["inventory"].asTable())
    {
        for (const auto& [item, count] : inventory->fields)
        {
            if (!items(item))
            {
                return Error{where(std::format("inventory: unknown item \"{}\"", item))};
            }
            c.addItem(item, static_cast<u32>(std::max<i64>(1, count.asInteger(1))));
        }
    }
    if (const script::Table* equipment = npc.fields["equipment"].asTable())
    {
        for (const script::Value& item : equipment->array)
        {
            c.addItem(item.asString());
            if (auto equipped = c.equip(item.asString(), items); !equipped)
            {
                return Error{where(std::format("equipment: {}", equipped.error().message))};
            }
        }
    }
    return c;
}

i32 Character::attribute(std::string_view name) const noexcept
{
    const auto it = m_attributes.find(name);
    return it == m_attributes.end() ? 0 : it->second;
}

Result<void> Character::setAttribute(std::string_view name, i32 value)
{
    if (!known(kAttributes, name))
    {
        return Error{std::format("unknown attribute '{}'", name)};
    }
    if (name == "hp" || name == "mana")
    {
        value = std::clamp(value, 0, attribute(name == "hp" ? "hp_max" : "mana_max"));
    }
    m_attributes[std::string(name)] = value;
    if (name == "hp_max" || name == "mana_max") // the current value never lies above the maximum
    {
        const std::string current(name.substr(0, name.size() - 4));
        m_attributes[current] = std::min(attribute(current), value);
    }
    return {};
}

i32 Character::talent(std::string_view name) const noexcept
{
    const auto it = m_talents.find(name);
    return it == m_talents.end() ? 0 : it->second;
}

Result<void> Character::setTalent(std::string_view name, i32 level)
{
    if (!known(kTalents, name))
    {
        return Error{std::format("unknown talent '{}'", name)};
    }
    m_talents[std::string(name)] = std::max(0, level);
    return {};
}

i32 Character::protection(std::string_view damageType) const noexcept
{
    const auto own = m_protection.find(damageType);
    i32 total = own == m_protection.end() ? 0 : own->second;
    for (const auto& slot : m_equippedProtection)
    {
        if (const auto it = slot.find(damageType); it != slot.end())
        {
            total += it->second;
        }
    }
    return total;
}

u32 Character::addExperience(i32 xp, const std::function<i32(i32 level)>& xpForLevel, i32 learnPointsPerLevel)
{
    m_xp += std::max(0, xp);
    u32 gained = 0;
    while (xpForLevel && m_xp >= xpForLevel(m_level + 1) && gained < 100)
    {
        ++m_level;
        ++gained;
        m_learnPoints += learnPointsPerLevel;
    }
    return gained;
}

void Character::addItem(std::string_view item, u32 count)
{
    if (count > 0)
    {
        m_items[std::string(item)] += count;
    }
}

bool Character::removeItem(std::string_view item, u32 count)
{
    const auto it = m_items.find(item);
    if (it == m_items.end() || it->second < count)
    {
        return false;
    }
    it->second -= count;
    if (it->second == 0)
    {
        m_items.erase(it);
    }
    // What is no longer there cannot stay equipped (one equipped per remaining item).
    u32 left = itemCount(item);
    for (usize s = 0; s < m_equipped.size(); ++s)
    {
        if (m_equipped[s] == item)
        {
            if (left > 0)
            {
                --left;
            }
            else
            {
                unequip(static_cast<EquipSlot>(s));
            }
        }
    }
    return true;
}

u32 Character::itemCount(std::string_view item) const noexcept
{
    const auto it = m_items.find(item);
    return it == m_items.end() ? 0 : it->second;
}

std::vector<ItemStack> Character::inventory(const ItemLookup& items) const
{
    std::vector<std::pair<usize, ItemStack>> ranked;
    for (const auto& [item, count] : m_items)
    {
        const auto info = items ? items(item) : std::nullopt;
        ranked.push_back({categoryRank(info ? info->category : "misc"), ItemStack{item, count}});
    }
    std::stable_sort(ranked.begin(), ranked.end(),
                     [](const auto& a, const auto& b) { return a.first < b.first; });
    std::vector<ItemStack> out;
    for (auto& [rank, stack] : ranked)
    {
        out.push_back(std::move(stack));
    }
    return out;
}

i32 Character::requirementValue(std::string_view name) const noexcept
{
    return known(kAttributes, name) ? attribute(name) : talent(name);
}

Result<EquipSlot> Character::equip(std::string_view item, const ItemLookup& items)
{
    const auto info = items ? items(item) : std::nullopt;
    if (!info)
    {
        return Error{std::format("unknown item \"{}\"", item)};
    }
    u32 alreadyEquipped = 0;
    for (const std::string& e : m_equipped)
    {
        alreadyEquipped += e == item ? 1 : 0;
    }
    if (itemCount(item) <= alreadyEquipped)
    {
        return Error{std::format("\"{}\" is not in the inventory", item)};
    }
    const auto first = slotFor(info->category);
    if (!first)
    {
        return Error{std::format("\"{}\" ({}) cannot be equipped", item, info->category)};
    }
    for (const auto& [name, needed] : info->requirements)
    {
        if (requirementValue(name) < needed)
        {
            return Error{std::format("\"{}\" needs {} {}", item, name, needed)};
        }
    }
    // Rings: the free one of two; runes and scrolls: the first free of seven; otherwise the one slot.
    EquipSlot slot = *first;
    const usize places = *first == EquipSlot::Ring1 ? 2 : *first == EquipSlot::Rune1 ? 7 : 1;
    for (usize i = 0; i < places; ++i)
    {
        const auto candidate = static_cast<EquipSlot>(static_cast<usize>(*first) + i);
        if (equipped(candidate).empty())
        {
            slot = candidate;
            break;
        }
    }
    const auto index = static_cast<usize>(slot);
    m_equipped[index] = std::string(item);
    m_equippedProtection[index] = info->protection;
    return slot;
}

void Character::unequip(EquipSlot slot) noexcept
{
    const auto index = static_cast<usize>(slot);
    m_equipped[index].clear();
    m_equippedProtection[index].clear();
}

const std::string& Character::equipped(EquipSlot slot) const noexcept
{
    return m_equipped[static_cast<usize>(slot)];
}
} // namespace g7::gameplay
