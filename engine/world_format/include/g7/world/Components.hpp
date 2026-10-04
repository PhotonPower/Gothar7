#pragma once

// Basic components of every vob (ADR 0005). Components are plain structs; a "vob" is any entity
// with a Vob component. Its local placement is a g7::Transform component (relative to the parent).

#include <g7/core/Math.hpp>
#include <g7/core/StringId.hpp>
#include <g7/core/Transform.hpp>
#include <g7/core/Types.hpp>

#include <compare>
#include <functional>
#include <string>

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
    /// Reserved (read and written, not used yet): a vob this trigger acts on - by id or by name.
    VobId targetId;
    std::string targetName;
    /// Level change (Gothic: change-level trigger): when the player enters, the engine loads this world
    /// (VFS path of a .g7world) and puts the player on its start point `changeStart`. Empty = none.
    std::string changeWorld;
    std::string changeStart;
};

/// A body of water (M5, contract with welt: world.md "Vob-Typen"): a box of halfExtents around the vob,
/// turned about Y only, not scaled. Its top (y + halfExtents.y) is the water surface. Boxes may overlap
/// (river sections); the highest surface wins.
struct WaterVolume
{
    Vec3 halfExtents{1.0f};
    std::string kind; ///< reserved (river, swamp ...): read and written, no effect yet
};

/// An interactive object (bed, chest, door; Gothic "Mob"). Placeholder until M8: drawn with its
/// MeshRef, `definition` names the mob definition (states, animations, focus name).
struct MobRef
{
    std::string definition;
};

/// An item lying in the world (M8): the Item instance of the scripts and how many. Drawn with the Item's
/// `mesh` (or a placeholder of its category); no collision, like Gothic.
struct ItemRef
{
    std::string instance;
    u32 count = 1;
    std::string owner; ///< Npc instance or guild it belongs to (taking it is theft); empty: nobody's
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
