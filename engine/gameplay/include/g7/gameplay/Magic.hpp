// Magic (M12 part B, owner decisions Z1-Z3): what a Spell instance says, and whether a character may cast it
// - a rune needs the spell's magic circle (talent magic_circle), a scroll anyone can read; both cost the
// spell's mana (Z3), which does not come back by itself (Z1: potions, sleep, levels).
#pragma once

#include <g7/core/Math.hpp>
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
    Telekinesis, ///< M17 part T: pulls an item to the caster while held
    Light,       ///< a glowing orb above the caster's hand for `duration` seconds (EngineSpellLight.cpp)
};

/// The orb of a light spell (Spell field `light`): its point light and where it hovers.
struct LightSpellLook
{
    Vec3 color{0.86f, 0.92f, 1.0f}; ///< linear
    f32 range = 8.0f;               ///< metres the light reaches
    f32 intensity = 3.0f;
    f32 flicker = 0.08f;    ///< 0..1, gentle
    f32 hover = 0.15f;      ///< metres above the open hand
    f32 shoulder = 0.25f;   ///< metres above the left shoulder while the hand is busy
    f32 warn = 10.0f;       ///< the last seconds it flickers more strongly
    f32 warnFlicker = 0.5f; ///< 0..1 then
    f32 fade = 1.0f;        ///< seconds it fades out at the end
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
    // Telekinesis (M17 part T): 0 = the TelekinesisSettings default
    f32 manaPerSecond = 0.0f;       ///< while held
    f32 range = 0.0f;               ///< metres to aim at an item
    f32 speed = 0.0f;               ///< m/s floating
    f32 lift = 0.0f;                ///< metres it rises first
    f32 carry = 0.0f;               ///< largest item extent (metres)
    std::optional<f32> blockedDrop; ///< seconds blocked until it falls (unset: the default; 0: never)
    LightSpellLook light;           ///< kind light
};

/// The Spell instance's fields.
[[nodiscard]] SpellInfo spellInfo(const script::Instance& spell);

/// Mana for a cast charged `stages` extra stages (Z5).
[[nodiscard]] i32 spellMana(const SpellInfo& spell, u32 stages) noexcept;

/// Why a cast fails (Z5); the engine words it for the player (text/ui.<language>.toml).
struct CastBlock
{
    enum class Reason
    {
        Circle, ///< the caster lacks the circle of magic `circle`
        Mana,   ///< not enough mana
    };
    Reason reason = Reason::Mana;
    i32 circle = 0;
};

/// Why `caster` cannot cast `spell` (from a rune, or a scroll) with `stages` - nullopt: it can.
[[nodiscard]] std::optional<CastBlock> castBlocked(const Character& caster, const SpellInfo& spell,
                                                   bool scroll, u32 stages = 0);
} // namespace g7::gameplay
