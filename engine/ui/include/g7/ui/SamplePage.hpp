#pragma once

// The UI sample page (M14 part A, `gothar --ui-sample`): every text role, both frames and the HUD bars in
// their places, for checking fonts, sizes and colours at any resolution.

namespace g7::ui
{
class Canvas;
class UiResources;

/// Draws the page over the whole canvas (between Canvas::begin and Canvas::end).
void drawSamplePage(Canvas& canvas, const UiResources& ui);
} // namespace g7::ui
