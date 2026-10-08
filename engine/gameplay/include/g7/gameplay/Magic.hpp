// Magic (M12 part B, owner decisions Z1-Z3): what a Spell instance says, and whether a character may cast it
// - a rune needs the spell's magic circle (talent magic_circle), a scroll anyone can read; both cost the
// spell's mana (Z3), which does not come back by itself (Z1: potions, sleep, levels).
#pragma once

#include <g7/core/Result.hpp>
#include <g7/core/Types.hpp>
#include <g7/gameplay/Character.hpp>

#include <map>
#include <optional>
#include <string>

namespace g7::script
{
struct Instance;
}

namespace g7::gameplay
{
enum class SpellKind : u8
{
    Projectile,
    Area,
    Self,
    Target,
    Summon,
    Transform,
};

struct SpellInfo
{
    std::string instance;
    std::string name;
    i32 circle = 1;
    i32 mana = 0;
    SpellKind kind = SpellKind::Projectile;
    u32 investStages = 0; ///< Z5: extra stages by charging (0: casts at once)
    i32 investMana = 0;   ///< per stage
    std::map<std::string, i32, std::less<>> damage;
    f32 radius = 0.0f;
    i32 heal = 0;
    std::string effect; ///< target spells: sleep, fear
    f32 duration = 0.0f;
    std::string summon;                                     ///< the Npc a summon calls
    std::string species;                                    ///< what a transformation turns into
    std::map<std::string, std::string, std::less<>> fx;     ///< cast, trail, impact, on_target
    bool burn = false;                                      ///< sets the target burning (decision B)
    std::map<std::string, std::string, std::less<>> sounds; ///< cast, impact (data/sounds.toml, M13)
};

/// The Spell instance's fields.
[[nodiscard]] SpellInfo spellInfo(const script::Instance& spell);

/// Mana for a cast charged `stages` extra stages (Z5).
[[nodiscard]] i32 spellMana(const SpellInfo& spell, u32 stages) noexcept;

/// Why `caster` cannot cast `spell` (from a rune, or a scroll) with `stages` - nullopt: it can. German text
/// for the player ("Dafür fehlt dir der zweite Kreis der Magie.").
[[nodiscard]] std::optional<std::string> castBlocked(const Character& caster, const SpellInfo& spell,
                                                     bool scroll, u32 stages = 0);
} // namespace g7::gameplay
