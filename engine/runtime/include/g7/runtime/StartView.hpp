#pragma once

// Reproducible views (development): the start options --cam, --yaw, --pitch, --fly and --player, and the
// line copy_position (F6) puts on the clipboard - so the owner, welt and figuren can open exactly the view
// someone reports (and take a --screenshot of it).

#include <g7/core/Math.hpp>
#include <g7/core/Result.hpp>
#include <g7/core/Types.hpp>
#include <g7/platform/Actions.hpp>

#include <optional>
#include <string>
#include <string_view>

namespace g7
{
/// Angles in degrees as the free camera and the player use them: yaw 0 looks along -Z, positive yaw turns
/// left (towards -X); pitch positive looks up.
struct StartView
{
    std::optional<Vec3> camera;      ///< --cam=x,y,z (m): the free camera there (fly mode with a player)
    std::optional<f32> yawDegrees;   ///< --yaw: of the camera, or of the player with --player
    std::optional<f32> pitchDegrees; ///< --pitch: of the camera
    bool fly = false;                ///< --fly: fly mode, the player waits at its start point
    std::optional<Vec3> player;      ///< --player=x,y,z (m): the player's feet

    [[nodiscard]] bool empty() const noexcept
    {
        return !camera && !yawDegrees && !pitchDegrees && !fly && !player;
    }
};

/// "103.2,4.1,-55" -> Vec3; errors for anything else (missing parts, letters, trailing text).
[[nodiscard]] Result<Vec3> parseVec3(std::string_view text);
/// A number with nothing around it.
[[nodiscard]] Result<f32> parseNumber(std::string_view text);
/// Takes `arg` into `view` if it is one of the view options: true when it was, false for other options,
/// an error for a view option with a bad value ("--cam=1,2").
[[nodiscard]] Result<bool> parseStartViewArgument(std::string_view arg, StartView& view);

/// What copy_position writes.
struct ViewLine
{
    std::string world;   ///< VFS path or file of the world; empty: no --world
    bool fly = true;     ///< fly mode: --cam/--yaw/--pitch/--fly; else the player: --player/--yaw
    Vec3 position{0.0f}; ///< camera (fly) or feet (player), metres
    f32 yawDegrees = 0.0f;
    f32 pitchDegrees = 0.0f;
    u32 minuteOfDay = 0;
};
/// E.g. "--world=worlds/leonberg/leonberg.g7world --cam=103.20,4.10,-55.00 --yaw=45.0 --pitch=-10.0
/// --time=12:00 --fly" (one line; positions in cm, angles in tenths of a degree, yaw in (-180, 180]).
[[nodiscard]] std::string formatViewLine(const ViewLine& line);
} // namespace g7

namespace g7
{
/// The key hint shown when fly mode starts, from the actual bindings (fly_*, copy_position, debug_fly), e.g.
/// "fly: WASD move, Space/LeftCtrl up/down, LeftShift fast, mouse look, wheel speed (10 m/s)\nF6 copy
/// position, F3 back"; parts whose actions have no input are left out.
[[nodiscard]] std::string flyHintText(const platform::ActionMap& actions, f32 speed);
} // namespace g7
