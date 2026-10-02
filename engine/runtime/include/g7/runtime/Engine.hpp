#pragma once

// Module: g7::runtime
// Verbindet alle Engine-Module zu einer lauffaehigen Anwendung: Initialisierungsreihenfolge,
// Hauptschleife (fester Simulationsschritt + interpoliertes Rendern), Shutdown.
// Spezifikation: docs/02-architecture.md ("Hauptschleife", "Initialisierung")

#include <g7/core/Result.hpp>
#include <g7/core/Types.hpp>
#include <g7/platform/Window.hpp>

#include <memory>
#include <string>

namespace g7
{
struct EngineConfig
{
    std::string appName = "Gothar";
    f64 simulationHz = 60.0;     ///< Fixed simulation rate.
    u64 maxFrames = 0;           ///< 0 = unlimited. Used by tests and headless runs.
    bool headless = false;       ///< No window (tests, CI smoke test).
    platform::WindowDesc window; ///< Used unless headless.
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

    void requestQuit() noexcept { m_quitRequested = true; }

    [[nodiscard]] u64 frameCount() const noexcept { return m_frameCount; }
    [[nodiscard]] u64 simulationTicks() const noexcept { return m_simTicks; }
    /// The game window, or nullptr when headless / before init().
    [[nodiscard]] platform::Window* window() noexcept { return m_window.get(); }

private:
    void shutdown();

    EngineConfig m_config;
    std::unique_ptr<platform::Window> m_window;
    bool m_initialized = false;
    bool m_quitRequested = false;
    u64 m_frameCount = 0;
    u64 m_simTicks = 0;
};
} // namespace g7
