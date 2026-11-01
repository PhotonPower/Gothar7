#pragma once

// Where things are on screen (M14 part B): anchored boxes in UI units (pixels at 1080p) and the screens'
// layout data, assets/source/ui/layout.toml. Screens name their boxes ("hud.hp", "sample.panel"); the values
// stay editable and are reloaded while the game runs.

#include <g7/core/Config.hpp>
#include <g7/core/Math.hpp>
#include <g7/core/Result.hpp>
#include <g7/core/Types.hpp>
#include <g7/ui/Canvas.hpp>

#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace g7::ui
{
/// Which point of the parent the box hangs on - the same point of the box: TopLeft puts the box's top left at
/// the parent's top left (plus the offset), Center centres it, BottomRight puts its bottom right at the
/// parent's.
enum class Anchor : u8
{
    TopLeft,
    Top,
    TopRight,
    Left,
    Center,
    Right,
    BottomLeft,
    Bottom,
    BottomRight,
};

/// "top_left", "top", "top_right", "left", "center", "right", "bottom_left", "bottom", "bottom_right".
[[nodiscard]] std::optional<Anchor> anchorFromName(std::string_view name) noexcept;

/// A box relative to its parent in UI units. +Y is down. A size <= 0 on an axis means the parent's extent
/// plus that value (0 = as wide or high as the parent, -64 = 64 less).
struct LayoutBox
{
    Anchor anchor = Anchor::TopLeft;
    Vec2 offset{0.0f};
    Vec2 size{0.0f};
};

/// The box in framebuffer pixels inside `parent` (pixels) at `scale` pixels per UI unit.
[[nodiscard]] Rect place(const LayoutBox& box, const Rect& parent, f32 scale) noexcept;

/// layout.toml, version 1: tables per screen with boxes (`anchor`, `offset = [x, y]`, `size = [w, h]`) and
/// numbers.
///
///     version = 1
///     [hud.hp]
///     anchor = "bottom_left"
///     offset = [32, -32]
///     size = [260, 22]
///     [inventory]
///     columns = 5
class Layout
{
public:
    [[nodiscard]] static Result<Layout> load(std::string_view toml,
                                             std::string_view sourceName = "ui/layout.toml");

    [[nodiscard]] bool contains(std::string_view key) const;
    /// The box at `key`; missing entries give `fallback` (reported once). Invalid anchors are reported.
    [[nodiscard]] LayoutBox box(std::string_view key, const LayoutBox& fallback = {}) const;
    [[nodiscard]] f32 number(std::string_view key, f32 fallback) const;
    [[nodiscard]] const Config& config() const noexcept { return m_config; }

private:
    void reportMissing(std::string_view key) const;

    Config m_config;
    mutable std::set<std::string, std::less<>> m_reported;
};
} // namespace g7::ui
