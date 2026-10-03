#pragma once

// The autopilot (gothar --walk): runs the player along a route, logs what happens on the way
// (walk.jsonl, walk_summary.json) and takes screenshots. A tool like the editor: the engine only knows
// EngineTool. Spec: docs/modules/tools.md "Autopilot".

#include <g7/core/FileSystem.hpp>
#include <g7/runtime/EngineTool.hpp>
#include <g7/walk/Route.hpp>

#include <memory>

namespace g7::walk
{
class Autopilot final : public EngineTool
{
public:
    /// Writes into `outDir` (created); screenshots only with rendering.
    Autopilot(Route route, fs::Path outDir);
    ~Autopilot() override;

    void update(Engine& engine, f64 realSeconds, bool uiMouse, bool uiKeyboard) override;
    void ui(Engine& engine, ui::DebugUi& debugUi) override;

    /// True once the route is done (or timed out); the engine is asked to quit then.
    [[nodiscard]] bool finished() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};
} // namespace g7::walk
