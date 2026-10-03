#pragma once

// Tools that run inside the engine (the editor, M4): the game registers them, the engine calls them
// every frame. runtime knows only this interface, not the tools (docs/02-architecture.md).

#include <g7/core/Types.hpp>

namespace g7::ui
{
class DebugUi;
}

namespace g7
{
class Engine;

class EngineTool
{
public:
    virtual ~EngineTool() = default;

    /// Once per frame after input and camera, before simulation and rendering. `uiMouse`/`uiKeyboard`:
    /// the debug UI uses the mouse or keyboard this frame (the tool should leave them alone then).
    virtual void update(Engine& engine, f64 realSeconds, bool uiMouse, bool uiKeyboard) = 0;
    /// Inside the debug UI frame (only while it is shown, F1): the tool's panels.
    virtual void ui(Engine& engine, ui::DebugUi& debugUi) = 0;
};
} // namespace g7
