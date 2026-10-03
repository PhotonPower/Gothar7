#pragma once

#include <g7/core/Config.hpp>
#include <g7/core/Types.hpp>
#include <g7/platform/Input.hpp>

#include <array>
#include <optional>
#include <span>
#include <string_view>
#include <variant>
#include <vector>

namespace g7::platform
{
/// Digital game actions. Game logic asks for actions, never for keys (docs/modules/platform.md).
/// The classic scheme uses the Gothic "Action" key (attack/use depending on direction and target);
/// the modern scheme uses separate Attack and Use instead.
enum class Action : u16
{
    MoveForward,
    MoveBack,
    StrafeLeft,
    StrafeRight,
    TurnLeft,
    TurnRight,
    Walk, ///< Held: walk instead of run (Gothic: running is the default).
    Sneak,
    Jump,
    Action,
    Attack,
    Use,
    DrawWeapon,
    DrawMagic,
    Inventory,
    Log,
    Status,
    Map,
    QuickSave,
    QuickLoad,
    Console,
    Pause,
    DebugDraw, ///< Development: toggles the debug-draw overlay.
    DebugUi,   ///< Development: toggles the ImGui debug panels.
    DebugFly,  ///< Development: switches between the player and the free debug camera.
    // Free camera (fly mode, F3 / --fly; also the editor): own keys, independent of the scheme.
    FlyForward,
    FlyBack,
    FlyLeft,
    FlyRight,
    FlyUp,
    FlyDown,
    FlyFast,
    CopyPosition, ///< Development: copies the view as start options (--world --cam ... --fly) to the
                  ///< clipboard.
    Count
};

/// Config names in snake_case: "move_forward", "draw_weapon", "quick_save" ...
[[nodiscard]] std::string_view name(Action action) noexcept;
/// Case-insensitive; nullopt for unknown names.
[[nodiscard]] std::optional<Action> actionFromName(std::string_view name) noexcept;

/// One physical input bound to an action.
using InputBinding = std::variant<Key, MouseButton, GamepadButton>;

/// Resolves "W", "MouseLeft", "PadSouth" ... (names from Input.hpp).
[[nodiscard]] std::optional<InputBinding> bindingFromName(std::string_view name) noexcept;
[[nodiscard]] std::string_view name(const InputBinding& binding) noexcept;

/// Action -> list of inputs. Several inputs per action; an action is active if any of them is.
class ActionMap
{
public:
    /// Reads table `bindings.<scheme>` with entries `action = ["W", "Up", "PadDpadUp"]`.
    /// Unknown actions, unknown input names and wrong value types are logged as warnings and
    /// skipped – a typo in a config file must not keep the game from starting.
    [[nodiscard]] static ActionMap fromConfig(const Config& config, std::string_view scheme);
    /// Writes every action (unbound ones as empty lists) to `bindings.<scheme>`.
    void writeTo(Config& config, std::string_view scheme) const;

    /// Adds a binding unless it is already present.
    void bind(Action action, InputBinding binding);
    void clear(Action action);
    [[nodiscard]] std::span<const InputBinding> bindings(Action action) const noexcept;

    /// Any bound input is held.
    [[nodiscard]] bool isDown(const Input& input, Action action) const noexcept;
    /// Any bound input went down this frame.
    [[nodiscard]] bool pressed(const Input& input, Action action) const noexcept;
    /// A bound input went up this frame and none is held any more.
    [[nodiscard]] bool released(const Input& input, Action action) const noexcept;

private:
    std::array<std::vector<InputBinding>, static_cast<usize>(Action::Count)> m_bindings;
};
} // namespace g7::platform
