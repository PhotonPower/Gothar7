#pragma once

#include <g7/asset/MeshData.hpp>

namespace g7::asset
{
/// Square plane in XZ at y = 0, facing +Y, centred on the origin; UVs repeat once per
/// `uvTileSize` metres. One material (white, or `color`). Ground for previews and tests.
[[nodiscard]] MeshData makePlane(f32 size, f32 uvTileSize = 1.0f, const Vec4& color = Vec4(1.0f));

/// Axis-aligned box centred on the origin with flat-shaded faces, UVs 0..1 per face and tangents.
[[nodiscard]] MeshData makeBox(const Vec3& halfExtents, const Vec4& color = Vec4(1.0f));
} // namespace g7::asset
