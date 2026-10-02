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

/// A vob showing a model (VFS path of a .g7mesh or, during development, a glTF).
struct MeshRef
{
    std::string path;
};

/// A point light (torch, camp fire). Colour is linear; flicker (0..1) is applied by the renderer later.
struct LightSource
{
    Vec3 color{1.0f, 0.62f, 0.3f};
    f32 range = 8.0f;
    f32 intensity = 3.0f;
    f32 flicker = 0.0f;
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
