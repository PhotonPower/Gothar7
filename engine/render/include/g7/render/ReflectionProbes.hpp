#pragma once

// Reflection probes (M17 windows part F2, render.md "Spiegelnde Fenster", stage B): small cube maps of the
// world around points along the streets, mirrored by window glass with window_reflections medium and high.
// The probes stand along the waynet (one every 20 m, 2.5 m above the ground, merged when nearer than 10 m);
// each has an influence box out to the facades around it for the parallax correction. The cube faces live as
// tiles in one atlas (RGBA16F, 64 x 64 texels a face); a fixed number of slots holds the probes nearest the
// camera. They are drawn a face at a time (render.md: one face a frame), nearest probe first, anew when the
// time of day or the weather has changed. The glass picks the nearest finished probes per pixel.
//
// ProbeSchedule is the bookkeeping (no GPU): which probe has which slot, which faces are due.
// ReflectionProbes adds the atlas and the face cameras; what a face shows (sky, terrain, buildings) the
// caller draws between beginFace() and endFace().

#include <g7/core/Geometry.hpp>
#include <g7/core/Math.hpp>
#include <g7/core/Result.hpp>
#include <g7/core/Types.hpp>
#include <g7/render/Camera.hpp>
#include <g7/render/Device.hpp>
#include <g7/render/Lighting.hpp>
#include <g7/render/rhi/Resources.hpp>

#include <array>
#include <span>
#include <utility>
#include <vector>

namespace g7::render
{
/// Where probes go (render.md, plan F2).
struct ProbePlacement
{
    f32 spacing = 20.0f; ///< m between probes along a path
    f32 height = 2.5f;   ///< m above the path
    f32 merge = 10.0f;   ///< m: nearer than this to a probe already placed, no new one
};

/// Probe positions: first the `junctions` (squares, crossings), then points every `spacing` along each
/// segment (both ends included), each kept only when no probe so far is nearer than `merge`; all raised by
/// `height`. Deterministic: same input, same probes in the same order.
[[nodiscard]] std::vector<Vec3> placeReflectionProbes(std::span<const Vec3> junctions,
                                                      std::span<const std::pair<Vec3, Vec3>> segments,
                                                      const ProbePlacement& placement = {});

/// A probe: where its faces are drawn from and the box its parallax correction assumes (the facades around).
struct ReflectionProbe
{
    Vec3 position{0.0f};
    AABB box;
};

/// The six cube faces: 0 +X, 1 -X, 2 +Y, 3 -Y, 4 +Z, 5 -Z. Face f looks along kProbeFaceForward[f] with
/// kProbeFaceUp[f] up; its image's x axis is cross(forward, up). Same tables as common/glass.glsl.
inline constexpr u32 kProbeFaces = 6;
inline const std::array<Vec3, kProbeFaces> kProbeFaceForward{Vec3(1, 0, 0),  Vec3(-1, 0, 0), Vec3(0, 1, 0),
                                                             Vec3(0, -1, 0), Vec3(0, 0, 1),  Vec3(0, 0, -1)};
inline const std::array<Vec3, kProbeFaces> kProbeFaceUp{Vec3(0, 1, 0), Vec3(0, 1, 0), Vec3(0, 0, -1),
                                                        Vec3(0, 0, 1), Vec3(0, 1, 0), Vec3(0, 1, 0)};
/// The camera of a face: at `position`, 90 degrees square, seeing `range` metres far.
[[nodiscard]] Camera probeFaceCamera(const Vec3& position, u32 face, f32 range) noexcept;

struct ProbeSettings
{
    u32 faceSize = 64;  ///< texels per face edge
    u32 slots = 96;     ///< probes held at once (the atlas: 8 slots a row of 6 faces each)
    f32 radius = 80.0f; ///< m: only probes this near the camera get a slot
    f32 range = 60.0f;  ///< m: how far a face sees (terrain and buildings beyond it are left out)
};

/// One face to draw: probe, its slot, which face, its tile in the atlas.
struct ProbeFace
{
    u32 probe = 0;
    u32 slot = 0;
    u32 face = 0;
    PixelRect tile;
};

/// The bookkeeping of the probes, without the GPU.
class ProbeSchedule
{
public:
    ProbeSchedule() = default;
    explicit ProbeSchedule(const ProbeSettings& settings) : m_settings(settings) {}

    /// A new world's probes: every slot is freed.
    void setProbes(std::vector<ReflectionProbe> probes);
    [[nodiscard]] std::span<const ReflectionProbe> probes() const noexcept { return m_probes; }
    /// The time of day or the weather changed: every face is drawn again (the old image is shown meanwhile).
    void invalidate() noexcept;
    /// The probes within the radius of `eye` (nearest first, at most `slots`) get slots, others lose theirs;
    /// then up to `budget` due faces, nearest probe first, into `out` (cleared).
    void update(const Vec3& eye, u32 budget, std::vector<ProbeFace>& out);
    /// A face was drawn.
    void faceDone(const ProbeFace& face) noexcept;
    /// The finished probes (all six faces drawn) nearest `eye` at the last update, at most kMaxShaderProbes.
    [[nodiscard]] std::span<const GpuProbe> shaderProbes() const noexcept { return m_shader; }

    [[nodiscard]] const ProbeSettings& settings() const noexcept { return m_settings; }
    [[nodiscard]] u32 slotsPerRow() const noexcept { return 8; }
    [[nodiscard]] PixelRect tile(u32 slot, u32 face) const noexcept;
    /// Slots in use and of them the finished ones (statistics, the "Render" window).
    [[nodiscard]] u32 slotsUsed() const noexcept;
    [[nodiscard]] u32 slotsReady() const noexcept;

private:
    struct Slot
    {
        u32 probe = ~0u; ///< ~0: free
        u8 drawn = 0;    ///< faces drawn at least once (bit per face)
        u8 stale = 0;    ///< faces to draw again (invalidate)
    };
    ProbeSettings m_settings;
    std::vector<ReflectionProbe> m_probes;
    std::vector<u32> m_slotOf; ///< per probe: its slot or ~0
    std::vector<Slot> m_slots;
    std::vector<std::pair<f32, u32>> m_near; ///< scratch: (distance², probe)
    std::vector<GpuProbe> m_shader;
};

/// The probes on the GPU: the atlas (colour and depth) and the schedule.
class ReflectionProbes
{
public:
    ReflectionProbes() = default;
    [[nodiscard]] static Result<ReflectionProbes> create(Device& device, const ProbeSettings& settings = {});

    [[nodiscard]] bool ready() const noexcept { return m_ready; }
    [[nodiscard]] ProbeSchedule& schedule() noexcept { return m_schedule; }
    [[nodiscard]] const ProbeSchedule& schedule() const noexcept { return m_schedule; }
    /// Binds the atlas and the face's tile (viewport and scissor) and clears the tile's depth; then the
    /// caller draws the face with probeFaceCamera().
    void beginFace(Device& device, const ProbeFace& face) const;
    /// Scissor off, the face counts as drawn.
    void endFace(Device& device, const ProbeFace& face);
    [[nodiscard]] const rhi::Texture& atlas() const noexcept { return m_color; }
    [[nodiscard]] const rhi::Framebuffer& framebuffer() const noexcept { return m_framebuffer; }
    /// Lighting block: x probes, y slots per row, z face size (texels).
    [[nodiscard]] Vec4 shaderParams() const noexcept;

private:
    ProbeSchedule m_schedule;
    rhi::Texture m_color;
    rhi::Texture m_depth;
    rhi::Framebuffer m_framebuffer;
    bool m_ready = false;
};
} // namespace g7::render
