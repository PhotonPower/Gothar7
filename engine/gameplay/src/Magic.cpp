#include <g7/gameplay/Magic.hpp>
#include <g7/script/ScriptVm.hpp>

#include <algorithm>
#include <array>
#include <format>

namespace g7::gameplay
{
SpellInfo spellInfo(const script::Instance& spell)
{
    SpellInfo s;
    s.instance = spell.name;
    s.name = std::string(spell.fields["name"].asString());
    s.circle = static_cast<i32>(spell.fields["circle"].asInteger(1));
    s.mana = static_cast<i32>(spell.fields["mana"].asInteger(0));
    const std::string_view kind = spell.fields["kind"].asString();
    s.kind = kind == "area"        ? SpellKind::Area
             : kind == "self"      ? SpellKind::Self
             : kind == "target"    ? SpellKind::Target
             : kind == "summon"    ? SpellKind::Summon
             : kind == "transform" ? SpellKind::Transform
                                   : SpellKind::Projectile;
    if (const script::Table* invest = spell.fields["invest"].asTable())
    {
        const auto stages = invest->fields.find("stages");
        const auto mana = invest->fields.find("mana");
        s.investStages = stages != invest->fields.end()
                             ? static_cast<u32>(std::max<i64>(0, stages->second.asInteger()))
                             : 0;
        s.investMana = mana != invest->fields.end() ? static_cast<i32>(mana->second.asInteger()) : 0;
    }
    if (const script::Table* damage = spell.fields["damage"].asTable())
    {
        for (const auto& [type, value] : damage->fields)
        {
            s.damage[type] = static_cast<i32>(value.asInteger());
        }
    }
    s.radius = static_cast<f32>(spell.fields["radius"].asNumber(0.0));
    s.heal = static_cast<i32>(spell.fields["heal"].asInteger(0));
    s.effect = std::string(spell.fields["effect"].asString());
    s.duration = static_cast<f32>(spell.fields["duration"].asNumber(0.0));
    s.summon = std::string(spell.fields["summon"].asString());
    s.species = std::string(spell.fields["species"].asString());
    if (const script::Table* fx = spell.fields["fx"].asTable())
    {
        for (const auto& [key, value] : fx->fields)
        {
            s.fx[key] = std::string(value.asString());
        }
    }
    return s;
}

i32 spellMana(const SpellInfo& spell, u32 stages) noexcept
{
    return spell.mana + static_cast<i32>(std::min(stages, spell.investStages)) * spell.investMana;
}

std::optional<std::string> castBlocked(const Character& caster, const SpellInfo& spell, bool scroll,
                                       u32 stages)
{
    static constexpr std::array<const char*, 6> kOrdinals = {"erste",  "zweite", "dritte",
                                                             "vierte", "fünfte", "sechste"};
    if (!scroll && caster.talent("magic_circle") < spell.circle)
    {
        return std::format("Dafür fehlt dir der {} Kreis der Magie.",
                           kOrdinals[static_cast<usize>(std::clamp(spell.circle, 1, 6) - 1)]);
    }
    if (caster.attribute("mana") < spellMana(spell, stages))
    {
        return std::string("Dafür reicht dein Mana nicht.");
    }
    return std::nullopt;
}
} // namespace g7::gameplay
