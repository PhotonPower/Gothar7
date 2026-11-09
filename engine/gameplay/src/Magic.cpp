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
    s.kind = kind == "area"          ? SpellKind::Area
             : kind == "self"        ? SpellKind::Self
             : kind == "target"      ? SpellKind::Target
             : kind == "summon"      ? SpellKind::Summon
             : kind == "transform"   ? SpellKind::Transform
             : kind == "telekinesis" ? SpellKind::Telekinesis
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
    s.burn = spell.fields["burn"].asBool();
    s.manaPerSecond = static_cast<f32>(spell.fields["mana_per_second"].asNumber(0.0));
    s.range = static_cast<f32>(spell.fields["range"].asNumber(0.0));
    s.speed = static_cast<f32>(spell.fields["speed"].asNumber(0.0));
    s.lift = static_cast<f32>(spell.fields["lift"].asNumber(0.0));
    s.carry = static_cast<f32>(spell.fields["carry"].asNumber(0.0));
    if (spell.fields["blocked_drop"].isNumber())
    {
        s.blockedDrop = static_cast<f32>(spell.fields["blocked_drop"].asNumber());
    }
    if (const script::Table* sounds = spell.fields["sounds"].asTable())
    {
        for (const auto& [role, name] : sounds->fields)
        {
            s.sounds[role] = std::string(name.asString());
        }
    }
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

std::optional<CastBlock> castBlocked(const Character& caster, const SpellInfo& spell, bool scroll, u32 stages)
{
    if (!scroll && caster.talent("magic_circle") < spell.circle)
    {
        return CastBlock{CastBlock::Reason::Circle, std::clamp(spell.circle, 1, 6)};
    }
    if (caster.attribute("mana") < spellMana(spell, stages))
    {
        return CastBlock{CastBlock::Reason::Mana, 0};
    }
    return std::nullopt;
}
} // namespace g7::gameplay
