#pragma once

// 3D item pictures for the inventory (M14 part D): every cell shows the item's model, rendered into a tile of
// an atlas texture (render to texture, twice the tile size for smooth edges, tonemapped with a fixed studio
// light) - docs/modules/ui.md "Plan M14" Teil D. The engine decides when a tile is (re)drawn: once per item,
// every frame for the turning selected one.

#include <g7/core/Math.hpp>
#include <g7/core/Result.hpp>
#include <g7/core/Types.hpp>
#include <g7/render/PostProcess.hpp>
#include <g7/render/rhi/Resources.hpp>
#include <g7/ui/Canvas.hpp>
#include <g7/ui/ItemCell.hpp>

#include <memory>

namespace g7::render
{
class Device;
class ShaderLibrary;
class MeshRenderer;
class Mesh;
class MaterialSet;
} // namespace g7::render

namespace g7::ui
{
class ItemPreviews
{
public:
    /// An atlas of `columns` x `columns` tiles of `tilePixels`.
    [[nodiscard]] static Result<ItemPreviews> create(render::Device& device, render::ShaderLibrary& shaders,
                                                     u32 tilePixels, u32 columns);

    ItemPreviews();
    ~ItemPreviews();
    ItemPreviews(ItemPreviews&&) noexcept;
    ItemPreviews& operator=(ItemPreviews&&) noexcept;

    [[nodiscard]] u32 capacity() const noexcept { return m_columns * m_columns; }
    [[nodiscard]] u32 tilePixels() const noexcept { return m_tile; }
    [[nodiscard]] const render::rhi::Texture& texture() const noexcept { return m_atlas; }
    /// The tile's texture coordinates (origin top left).
    [[nodiscard]] Rect uv(u32 tile) const noexcept;

    /// Draws a model into `tile` (cleared first), spun about the view's Y axis by `spinRadians`. Leaves
    /// another framebuffer bound: the caller binds its target again.
    void render(render::Device& device, render::MeshRenderer& meshes, u32 tile, const render::Mesh& mesh,
                const render::MaterialSet& materials, const CellFrame& frame, f32 spinRadians,
                const PreviewLight& light);
    /// Empties a tile (an item without a model).
    void clear(render::Device& device, u32 tile);

private:
    struct Gpu;
    std::unique_ptr<Gpu> m_gpu;
    render::rhi::Texture m_atlas;
    u32 m_tile = 0;
    u32 m_columns = 0;
};
} // namespace g7::ui
