#pragma once

// Basic components of every vob (ADR 0005). Components are plain structs; a "vob" is any entity
// with a Vob component. Its local placement is a g7::Transform component (relative to the parent).

#include <g7/core/Math.hpp>
#include <g7/core/StringId.hpp>
#include <g7/core/Transform.hpp>
#include <g7/core/Types.hpp>

#include <compare>
#include <functional>

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

/// Every placed object.
struct Vob
{
    VobId id;
    StringId name;
};

/// World matrix of a vob (parent world * local), kept up to date by World::updateTransforms().
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
