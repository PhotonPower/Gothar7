#pragma once

// Cooked clip sets, ".g7anim" (ADR 0025, plan M17 cooked figures part C): AnimationSetData as it comes from
// loadAnimationGltf, without a glTF parser at load time. The events and speeds of <set>.events.toml stay in
// their own file and are applied after loading, as with glTF (applyClipEvents).

#include <g7/asset/SkinnedModel.hpp>
#include <g7/core/Result.hpp>
#include <g7/core/Types.hpp>

#include <span>
#include <string_view>
#include <vector>

namespace g7::asset
{
/// ".g7anim" version 1, little-endian; strings are u16 length + UTF-8, vectors 3/4 x f32,
/// quaternions x y z w:
///
///     Header    magic "G7AN", version u32
///     Skeleton  boneCount u32; per bone: name, parent i32, translation, rotation, scale; rootParent 16 x f32
///     Clips     count u32; per clip: name, duration f32, trackCount u32; per track: bone name, path u8
///               (0 translation, 1 rotation, 2 scale), step u8, keyCount u32, times f32, values 4 x f32
///
/// Clip events and speeds are not stored (events.toml).
inline constexpr char kAnimMagic[4] = {'G', '7', 'A', 'N'};
inline constexpr u32 kAnimVersion = 1;

[[nodiscard]] std::vector<u8> serializeAnim(const AnimationSetData& set);
/// Parses and validates: counts against the data, bone parents before children, known channels, rising key
/// times, durations not negative.
[[nodiscard]] Result<AnimationSetData> deserializeAnim(std::span<const u8> bytes,
                                                       std::string_view debugName = "<memory>");
} // namespace g7::asset
