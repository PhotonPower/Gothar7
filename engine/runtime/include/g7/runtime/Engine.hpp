#pragma once

// Module: g7::runtime
// Verbindet alle Engine-Module zu einer lauffaehigen Anwendung: Initialisierungsreihenfolge,
// Hauptschleife (fester Simulationsschritt + interpoliertes Rendern), Shutdown.
// Spezifikation: docs/02-architecture.md ("Hauptschleife", "Initialisierung")

#include <g7/core/Clock.hpp>
#include <g7/core/Config.hpp>
#include <g7/core/Result.hpp>
#include <g7/core/Types.hpp>
#include <g7/platform/Actions.hpp>
#include <g7/platform/Input.hpp>
#include <g7/platform/Window.hpp>

#include <memory>
#include <string>

namespace g7
{
struct EngineConfig
{
    std::string appName = "Gothar";
    f64 simulationHz = 60.0; ///< Fixed simulation rate.
    u64 maxFrames = 0;       ///< 0 = unlimited. Used by tests and headless runs.
    f64 maxFps = 0.0;        ///< Frame-rate cap with a window; 0 = unlimited. Headless is never capped.
    /// > 0: every frame advances exactly this much time instead of real time (deterministic
    /// tests and CI runs).
    f64 fixedFrameSeconds = 0.0;
    bool headless = false;       ///< No window (tests, CI smoke test).
    platform::WindowDesc window; ///< Used unless headless.
    /// Merged settings (engine.toml + user config). The engine reads [input] (scheme,
    /// stick_deadzone) and [bindings.<scheme>]; window settings are applied by the caller.
    Config settings;
};

class Engine
{
public:
    explicit Engine(EngineConfig config);
    ~Engine();

    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;

    /// Initializes all subsystems in dependency order.
    [[nodiscard]] Result<void> init();

    /// Runs the main loop until quit is requested or maxFrames is reached.
    /// Returns the process exit code.
    int run();
    /// Runs a single frame (events, simulation steps, render, frame cap). Returns false once
    /// quitting was requested. run() is a loop over this; tests use it to step frame by frame.
    [[nodiscard]] bool runFrame();

    void requestQuit() noexcept { m_quitRequested = true; }

    [[nodiscard]] u64 frameCount() const noexcept { return m_frameCount; }
    [[nodiscard]] u64 simulationTicks() const noexcept { return m_simTicks; }
    /// Simulated time in seconds (ticks * step): stands still while paused, follows timeScale.
    [[nodiscard]] f64 simulationTime() const noexcept
    {
        return static_cast<f64>(m_simTicks) * m_fixedStep.step();
    }
    /// Interpolation factor between the last two simulation states, for rendering.
    [[nodiscard]] f64 frameAlpha() const noexcept { return m_fixedStep.alpha(); }

    /// Speed of simulated time relative to real time, clamped to [0, kMaxTimeScale].
    void setTimeScale(f64 scale) noexcept;
    [[nodiscard]] f64 timeScale() const noexcept { return m_timeScale; }
    /// Paused: events and rendering continue, the simulation does not advance and does not
    /// catch up afterwards.
    void setPaused(bool paused) noexcept;
    [[nodiscard]] bool paused() const noexcept { return m_paused; }
    static constexpr f64 kMaxTimeScale = 10.0;

    /// The game window, or nullptr when headless / before init().
    [[nodiscard]] platform::Window* window() noexcept { return m_window.get(); }
    /// Input state of the current frame (stays empty when headless).
    [[nodiscard]] const platform::Input& input() const noexcept { return m_input; }
    /// Bindings of the active control scheme.
    [[nodiscard]] const platform::ActionMap& actions() const noexcept { return m_actions; }

private:
    void shutdown();

    EngineConfig m_config;
    std::unique_ptr<platform::Window> m_window;
    platform::Input m_input;
    platform::ActionMap m_actions;
    FixedStep m_fixedStep{1.0 / 60.0};
    FramePacer m_framePacer{0.0};
    Stopwatch m_frameTimer;
    f64 m_timeScale = 1.0;
    bool m_paused = false;
    bool m_initialized = false;
    bool m_quitRequested = false;
    u64 m_frameCount = 0;
    u64 m_simTicks = 0;
};
} // namespace g7
