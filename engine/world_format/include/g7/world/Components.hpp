#pragma once

// Basic components of every vob (ADR 0005). Components are plain structs; a "vob" is any entity
// with a Vob component. Its local placement is a g7::Transform component (relative to the parent).

#include <g7/core/Math.hpp>
#include <g7/core/StringId.hpp>
#include <g7/core/Transform.hpp>
#include <g7/core/Types.hpp>

#include <array>
#include <compare>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace g7::world
{
/// Persistent id of a vob (ADR 0005, contract with the world track): unique within a world, never
/// reused, stored in .g7world; 0 = none. World vobs get ids from the world's nextVobId counter,
/// vobs spawned at runtime from kRuntimeVobIdBase upwards (saved in the save game).
struct VobId
{
    u64 value = 0;

    [[nodiscard]] constexpr bool valid() const noexcept { return value != 0; }
    [[nodiscard]] constexpr bool runtime() const noexcept;
    friend constexpr auto operator<=>(VobId, VobId) noexcept = default;
};

inline constexpr u64 kRuntimeVobIdBase = 0x8000'0000'0000'0000ull;

constexpr bool VobId::runtime() const noexcept
{
    return value >= kRuntimeVobIdBase;
}

/// Every placed object. The name is kept as text too: StringId keeps its text only in debug builds,
/// but saving a world needs it.
struct Vob
{
    VobId id;
    StringId name;
    std::string nameText;
};

/// How a drawn vob may be hidden at a distance (render.md "Sichtbarkeit").
enum class VobCategory : u8
{
    Deco,     ///< scenery: hidden when small on screen (size_cull) and beyond view_distance
    Gameplay, ///< must not vanish for its size (mobs, later NPCs, items): only view_distance applies
};

/// A vob showing a model (VFS path of a .g7mesh or, during development, a glTF).
struct MeshRef
{
    std::string path;
    VobCategory category = VobCategory::Deco;
};

/// A point light (torch, camp fire). Colour is linear; flicker (0..1) is applied by the renderer later.
struct LightSource
{
    Vec3 color{1.0f, 0.62f, 0.3f};
    f32 range = 8.0f;
    f32 intensity = 3.0f;
    f32 flicker = 0.0f;
    /// Daylight through a window (contract with welt, world.md): colour of the sky's ambient at the time of
    /// day, intensity times the sky's brightness relative to noon (night: about 0), no flicker.
    bool daylight = false;
};

/// Where the player (and, without a player, the camera) starts. The vob's placement is the feet; the
/// camera sits kStartEyeHeight above it. Selected by name (--start) or the lowest id.
struct StartPoint
{
    u8 reserved = 0; ///< not empty: EnTT keeps no instance of empty components (Scene::get)
};

inline constexpr f32 kStartEyeHeight = 1.62f; // measured on the figures (figuren, M5)

/// A sound source placed in the world (like ZenGin zCVobSound). Played once the audio module exists;
/// `sound` names a sound definition (data, not a file path).
struct SoundEmitter
{
    enum class Mode : u8
    {
        Loop,   ///< plays continuously
        Random, ///< plays now and then, `delay` seconds apart
    };
    std::string sound;
    f32 range = 20.0f; ///< metres until silent
    f32 volume = 1.0f; ///< 0..1
    Mode mode = Mode::Loop;
    Vec2 delay{5.0f, 15.0f}; ///< min, max seconds between plays (Random)
};

/// Actions of the event net (dungeon.md "Ereignisse und Ziele"): what a trigger or relay sends to its
/// targets. `trigger` is the plain "fire" (a door toggles, a relay counts an input).
inline constexpr std::string_view kEventTrigger = "trigger";
inline constexpr std::array<std::string_view, 9> kEventActions = {
    "toggle", "open", "close", "on", "off", "wake", "trigger", "lock", "unlock"};

[[nodiscard]] constexpr bool isEventAction(std::string_view action) noexcept
{
    for (const std::string_view a : kEventActions)
    {
        if (a == action)
        {
            return true;
        }
    }
    return false;
}

/// The opposite action (open <-> close, on <-> off, lock <-> unlock); the others are their own opposite.
[[nodiscard]] constexpr std::string_view inverseEventAction(std::string_view action) noexcept
{
    constexpr std::array<std::pair<std::string_view, std::string_view>, 6> kPairs = {{{"open", "close"},
                                                                                      {"close", "open"},
                                                                                      {"on", "off"},
                                                                                      {"off", "on"},
                                                                                      {"lock", "unlock"},
                                                                                      {"unlock", "lock"}}};
    for (const auto& [a, b] : kPairs)
    {
        if (a == action)
        {
            return b;
        }
    }
    return action;
}

/// False for the "off" side (close, off, lock): a relay input goes off, a pressed plate is released.
[[nodiscard]] constexpr bool isPositiveEventAction(std::string_view action) noexcept
{
    return action != "close" && action != "off" && action != "lock";
}

/// A relay (vob type "relay", like ZenGin's zCTriggerList; dungeon.md): collects events sent to it and passes
/// `action` on to `targets`. Invisible, no volume.
struct Relay
{
    enum class Mode : u8
    {
        Any,   ///< every event goes on (an "off" event as the opposite action)
        All,   ///< on once every input is on, the opposite once one goes off again
        Count, ///< after `count` events (the "on" side), then counts anew
    };
    Mode mode = Mode::Any;
    u32 count = 1;       ///< Count: events needed
    f32 delay = 0.0f;    ///< seconds until what it sends arrives
    bool once = false;   ///< fires one time only
    bool invert = false; ///< sends the opposite action (a plate holds a gate open)
    std::vector<std::string> targets;
    std::string action = std::string(kEventTrigger);
};

/// A moving part (dungeon D2, `components.mover` at a mesh or mob vob, like ZenGin's zCMover): a portcullis,
/// a secret wall or a trapdoor. It moves between its closed pose (the vob's transform in the file) and its
/// open pose when events reach it (open, close, toggle, trigger; on/off as open/close); its collision moves
/// with it. Each kind brings its motion, worked out from the model's bounds; `offset`, `axis` and `angle`
/// replace parts of it.
struct Mover
{
    enum class Kind : u8
    {
        Gate,       ///< slides up by the model's height (a portcullis)
        SecretWall, ///< slides back (-Z) by the model's depth
        Trapdoor,   ///< turns 90 degrees about the local +X axis through the origin: its leaf (+Z) goes down
    };
    /// What happens when someone is in the way while it closes (dungeon.md questions 2 and 3).
    enum class Crush : u8
    {
        Stop,   ///< it stops and opens again, no harm
        Damage, ///< a trap: 20 damage to whoever is in the way, then it opens again
    };
    Kind kind = Kind::Gate;
    bool open = false;          ///< open from the start
    f32 seconds = 2.0f;         ///< for the whole way (the default depends on the kind, see kMoverSeconds)
    std::optional<Vec3> offset; ///< metres in the vob's frame (turned, not scaled): replaces the slide
    std::optional<Vec3> axis;   ///< in the vob's frame through its origin: replaces the turn axis
    std::optional<f32> angle;   ///< degrees about `axis` (trapdoor: 90)
    std::string soundMove;      ///< played where it is when it starts moving
    std::string soundStop;      ///< ... when it arrives
    Crush crush = Crush::Stop;

    bool operator==(const Mover&) const = default;
};

/// Seconds for the way when the file gives none: gate, secret wall, trapdoor.
inline constexpr std::array<f32, 3> kMoverSeconds = {2.0f, 3.0f, 0.6f};

/// A pressure plate (dungeon D3, `components.trigger.plate`): the trigger is pressed while something lies or
/// stands in it; the first weight sends the trigger's `action` to its `targets`, the last one leaving sends
/// `releaseAction` (empty: the opposite of `action`).
struct Plate
{
    enum class Weight : u8
    {
        Any,      ///< the hero, NPCs, animals and items lying on it
        Creature, ///< the hero, NPCs and animals
        Player,   ///< the hero only
    };
    Weight weight = Weight::Any;
    std::string releaseAction; ///< empty: inverseEventAction(action)

    bool operator==(const Plate&) const = default;
};

/// A volume that reports who enters and leaves it (TriggerSystem). Callbacks name script functions;
/// the engine passes the events on (log in M4, Lua from M7).
struct TriggerVolume
{
    enum class Shape : u8
    {
        Box,    ///< halfExtents around the vob, turned with it
        Sphere, ///< radius around the vob
    };
    enum class Filter : u8
    {
        Player,
        Npc,
        Any,
    };
    Shape shape = Shape::Box;
    Vec3 halfExtents{1.0f};
    f32 radius = 1.0f;
    std::string onEnter;
    std::string onLeave;
    Filter filter = Filter::Player;
    bool once = false; ///< reports the first enter only (and the matching leave)
    /// Event net (dungeon.md D1): when the player enters, `action` goes to every vob named in `targets`
    /// (doors, relays, later movers; other names reach Lua as dungeon_event). The old `target` (a name) is
    /// read as one target.
    std::vector<std::string> targets;
    std::string action = std::string(kEventTrigger);
    /// Pressure plate (dungeon D3): sends on weight instead of on the player's entering.
    std::optional<Plate> plate;
    /// The old reserved `target` given as a vob id (written back as it was; sends like a name target).
    VobId targetId;
    /// Level change (Gothic: change-level trigger): when the player enters, the engine loads this world
    /// (VFS path of a .g7world) and puts the player on its start point `changeStart`. Empty = none.
    std::string changeWorld;
    std::string changeStart;
    /// Private area (M9, Gothic: a hut's room): the Npc instance or guild it belongs to. The player entering
    /// it is perceived by that owner (assess_enter_room). Empty: nobody's.
    std::string owner;
};

/// A body of water (M5, contract with welt: world.md "Vob-Typen"): a box of halfExtents around the vob,
/// turned about Y only, not scaled. Its top (y + halfExtents.y) is the water surface. Boxes may overlap
/// (river sections); the highest surface wins.
struct WaterVolume
{
    Vec3 halfExtents{1.0f};
    std::string
        kind; ///< "river" (flows along local +X) or "pond"; empty = the default of data/water.toml (M17)
};

/// An interactive object (bed, chest, door; Gothic "Mob"). Placeholder until M8: drawn with its
/// MeshRef, `definition` names the mob definition (states, animations, focus name).
struct MobRef
{
    std::string definition;
    /// Doors (contract with welt, world.md): stored closed (the vob's rotation), open = +90 degrees about the
    /// hinge, the leaf into the room (away from the front, +Z outside). Written only when true.
    bool open = false;
    /// Switches (dungeon D3: mob types lever and wall_button): what using it sends. A lever sends `action`
    /// when thrown and its opposite when thrown back; a button sends `action` at each press (`once`: only the
    /// first). Written after `open` and only when there are targets.
    std::vector<std::string> targets;
    std::string action = std::string(kEventTrigger);
    bool once = false;   ///< a button: one press only (written when true)
    bool hidden = false; ///< in focus only from close by and as a loose stone until found (written when true)
};

/// An item lying in the world (M8): the Item instance of the scripts and how many. Drawn with the Item's
/// `mesh` (or a placeholder of its category); no collision, like Gothic.
struct ItemRef
{
    std::string instance;
    u32 count = 1;
    std::string owner; ///< Npc instance or guild it belongs to (taking it is theft); empty: nobody's
};

/// What one walks on in a mesh or mob vob (M13 E, `components.surface`): its footstep material.
struct SurfaceRef
{
    std::string footstep; ///< "wood", "stone" ... (data/footsteps.toml)
};

/// World matrix of a vob (parent world * local), kept up to date by Scene::updateTransforms().
struct WorldTransform
{
    Mat4 matrix{1.0f};
};
} // namespace g7::world

template <>
struct std::hash<g7::world::VobId>
{
    std::size_t operator()(g7::world::VobId id) const noexcept { return std::hash<g7::u64>{}(id.value); }
};
