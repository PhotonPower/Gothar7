#pragma once

// The game state of a save game as plain data and as a JSON document (M15 part B, docs/modules/save.md): what
// the engine captures and applies - time, Story, named timers, the hero, and per world its delta against the
// file, its mobs, NPCs and spent triggers. The file container (.g7save, ADR 0021) wraps this document later.

#include <g7/core/Math.hpp>
#include <g7/core/Result.hpp>
#include <g7/core/Types.hpp>
#include <g7/gameplay/Character.hpp>
#include <g7/save/WorldDelta.hpp>
#include <g7/script/ScriptVm.hpp>
#include <g7/script/Value.hpp>

#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace g7::save
{
/// Version of the document; older ones are migrated when read (none yet).
inline constexpr u32 kGameStateVersion = 1;

struct MobState
{
    u64 vob = 0;
    std::string definition; ///< checked when applied
    bool locked = false;
    bool open = false;
    std::map<std::string, u32, std::less<>> contents;
    friend bool operator==(const MobState&, const MobState&) = default;
};

enum class Life : u8
{
    Alive,
    Down,     ///< knocked out, gets up after `downSeconds`
    Dead,     ///< lies as a body (lootable)
    Vanished, ///< gone (a summoned creature after its time)
};

/// An NPC or animal (Npc instance). Its AI is not saved: it begins the state of its routine anew (plan point
/// 2).
struct NpcState
{
    std::string name;     ///< for scripts: "npc_gate_guard", "mon_wolf#2"
    std::string instance; ///< the Npc instance
    Vec3 position{0.0f};  ///< feet
    f32 yaw = 0.0f;
    std::optional<gameplay::CharacterState> character;
    std::string routine; ///< rtn_..., empty: none
    Life life = Life::Alive;
    f32 downSeconds = 0.0f;
    bool pickpocketed = false;
    bool summoned = false;
    f32 summonSeconds = 0.0f;
    friend bool operator==(const NpcState&, const NpcState&) = default;
};

struct WorldState
{
    std::string path; ///< VFS path of the .g7world
    WorldDelta delta;
    std::vector<u64> spentTriggers; ///< once-triggers that fired
    std::vector<MobState> mobs;     ///< by vob id
    std::vector<NpcState> npcs;     ///< in the order they were put in
};

struct HeroState
{
    gameplay::CharacterState character;
    Vec3 position{0.0f}; ///< feet
    f32 yaw = 0.0f;
    std::string torch; ///< the lit torch's item, empty: none
};

struct GameState
{
    u32 version = kGameStateVersion;
    f64 clock = 0.0;            ///< world::GameTime::clock (minutes since day 0)
    f64 secondsPerMinute = 4.0; ///< game time scale
    f64 scriptClock = 0.0;      ///< ScriptVm::time
    u32 nextTimerId = 1;
    std::vector<script::ScriptVm::NamedTimer> timers;
    script::Value story; ///< the table Story
    std::optional<HeroState> hero;
    std::string currentWorld;
    std::vector<WorldState> worlds; ///< every visited world, the current one included
};

/// Stable text: the same state always gives the same bytes (tests: capture -> apply -> capture).
[[nodiscard]] std::string toJson(const GameState& state);
/// Reads what toJson wrote. Errors name the entry ("worlds[0].npcs[2]: missing 'name'").
[[nodiscard]] Result<GameState> fromJson(std::string_view json);
} // namespace g7::save
