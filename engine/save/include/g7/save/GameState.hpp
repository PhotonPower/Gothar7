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
#include <g7/world/EventNet.hpp>

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
    bool found = false; ///< dungeon D3: a hidden switch was used once (JSON only when true)
    bool spent = false; ///< dungeon D3: a once-button was pressed (JSON only when true)
    friend bool operator==(const MobState&, const MobState&) = default;
};

/// A moving part (dungeon D2) away from its initial state: where it stands and where it goes.
struct MoverState
{
    u64 vob = 0;
    f32 position = 0.0f; ///< 0 closed .. 1 open
    bool open = false;   ///< where it goes
    friend bool operator==(const MoverState&, const MoverState&) = default;
};

/// A teleporter (dungeon D4) switched away from how the world file starts it.
struct TeleporterState
{
    u64 vob = 0;
    bool active = true;
    friend bool operator==(const TeleporterState&, const TeleporterState&) = default;
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
    /// The event net (dungeon D1): relays not in their initial state, events on their way. Additive: older
    /// saves have none (the world's initial state).
    world::EventNetSnapshot events;
    /// Moving parts not in their initial state (dungeon D2), by vob id. Additive like `events`.
    std::vector<MoverState> movers;
    /// Teleporters switched on or off against the file (dungeon D4), by vob id. Additive.
    std::vector<TeleporterState> teleporters;
};

struct HeroState
{
    gameplay::CharacterState character;
    Vec3 position{0.0f}; ///< feet
    f32 yaw = 0.0f;
    std::string torch; ///< the lit torch's item, empty: none
};

/// The weather (M17 E1, world::WeatherSnapshot); older saves have none - the game keeps its weather then.
struct WeatherSave
{
    std::string state;
    std::string previous;
    f64 changeMinute = 0.0;
    f64 fadeMinutes = 0.0;
    f64 nextMinute = 0.0;
    u64 random = 0;
    bool locked = false;
    f32 wetness = 0.0f; ///< M17 N; saves of E1 have none (0)
    f64 wetnessClock = 0.0;
    friend bool operator==(const WeatherSave&, const WeatherSave&) = default;
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
    std::vector<WorldState> worlds;     ///< every visited world, the current one included
    std::optional<WeatherSave> weather; ///< M17 E1; additive (older saves: none)
};

/// Stable text: the same state always gives the same bytes (tests: capture -> apply -> capture).
[[nodiscard]] std::string toJson(const GameState& state);
/// Reads what toJson wrote. Errors name the entry ("worlds[0].npcs[2]: missing 'name'").
[[nodiscard]] Result<GameState> fromJson(std::string_view json);
/// The same in parts, for the chunks of a save file: everything but the worlds, and one world.
[[nodiscard]] std::string globalJson(const GameState& state);
[[nodiscard]] std::string worldJson(const WorldState& world);
[[nodiscard]] Result<GameState> parseGlobalJson(std::string_view json); ///< worlds stay empty
[[nodiscard]] Result<WorldState> parseWorldJson(std::string_view json, std::string_view where = "world");
} // namespace g7::save
