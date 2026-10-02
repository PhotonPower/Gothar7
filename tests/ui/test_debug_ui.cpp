// Debug UI without a GPU: input reaches ImGui, capture flags, scissor conversion. The suite links
// ImGui itself to inspect what DebugUi fed into it.

#include <g7/platform/Input.hpp>
#include <g7/ui/DebugUi.hpp>

#include <doctest/doctest.h>
#include <imgui.h>

#include <array>
#include <cstring>
#include <ostream>

using namespace g7;
using namespace g7::ui;
using platform::Input;
using platform::Key;
using platform::MouseButton;

namespace
{
const Vec2 kSize(1280.0f, 720.0f);

DebugUi makeUi()
{
    auto ui = DebugUi::create(nullptr, nullptr, 1.0f);
    REQUIRE(ui.ok());
    return std::move(ui).value();
}

/// One frame with the engine panel; `between` runs extra ImGui code inside the frame.
template <typename F>
void frame(DebugUi& ui, Input& input, F&& between)
{
    ui.beginFrame(input, kSize, kSize, 1.0f / 60.0f);
    EnginePanel panel;
    ui.enginePanel(panel);
    between();
    ui.endFrame(nullptr);
    input.beginFrame();
}

void frame(DebugUi& ui, Input& input)
{
    frame(ui, input, [] {});
}
} // namespace

TEST_CASE("DebugUi: scissor rectangles from ImGui clip rects")
{
    // y grows downwards in ImGui, upwards for GL scissors.
    const auto rect = scissorFromClip(Vec4(10, 20, 110, 70), 200, 100);
    REQUIRE(rect.has_value());
    CHECK(rect->x == 10);
    CHECK(rect->y == 30);
    CHECK(rect->width == 100);
    CHECK(rect->height == 50);

    const auto clamped = scissorFromClip(Vec4(-50, -10, 500, 300), 200, 100);
    REQUIRE(clamped.has_value());
    CHECK(clamped->x == 0);
    CHECK(clamped->y == 0);
    CHECK(clamped->width == 200);
    CHECK(clamped->height == 100);

    CHECK_FALSE(scissorFromClip(Vec4(50, 50, 50, 80), 200, 100).has_value());   // zero width
    CHECK_FALSE(scissorFromClip(Vec4(300, 10, 400, 20), 200, 100).has_value()); // outside
}

TEST_CASE("DebugUi: the panel captures the mouse only above it")
{
    DebugUi ui = makeUi();
    Input input;
    input.onMouseMotion(Vec2(1000, 600), Vec2(0));
    frame(ui, input);
    frame(ui, input);
    CHECK_FALSE(ui.wantsMouse());

    input.onMouseMotion(Vec2(60, 60), Vec2(0)); // inside the panel at the top left
    frame(ui, input);
    frame(ui, input);
    CHECK(ui.wantsMouse());
    CHECK_FALSE(ui.wantsKeyboard());
}

TEST_CASE("DebugUi: keys, buttons, wheel and text reach ImGui")
{
    DebugUi ui = makeUi();
    Input input;
    frame(ui, input);

    input.onKey(Key::A, true);
    input.onKey(Key::A, false); // tapped within one frame
    input.onKey(Key::LeftCtrl, true);
    input.onMouseButton(MouseButton::Left, true);
    input.onWheel(2.0f);
    // ImGui spreads several changes of one frame over the following frames ("trickle"), so a tap
    // is never lost; collect over a few frames.
    bool tapped = false;
    bool ctrl = false;
    bool clicked = false;
    f32 wheel = 0.0f;
    for (int i = 0; i < 4; ++i)
    {
        frame(ui, input,
              [&]
              {
                  tapped = tapped || ImGui::IsKeyPressed(ImGuiKey_A, false);
                  ctrl = ctrl || ImGui::GetIO().KeyCtrl;
                  clicked = clicked || ImGui::IsMouseClicked(ImGuiMouseButton_Left);
                  wheel += ImGui::GetIO().MouseWheel;
              });
    }
    CHECK(tapped);
    CHECK(ctrl);
    CHECK(clicked);
    CHECK(wheel == doctest::Approx(2.0f));
    CHECK_FALSE(ui.wantsKeyboard()); // no text field: the game keeps the keyboard

    // Release everything: Ctrl would turn typing into shortcuts.
    input.onKey(Key::LeftCtrl, false);
    input.onMouseButton(MouseButton::Left, false);
    frame(ui, input);

    // A focused text field wants text and receives it as UTF-8.
    std::array<char, 64> buffer{};
    const auto field = [&]
    {
        ImGui::Begin("text");
        if (ImGui::IsWindowAppearing())
        {
            ImGui::SetKeyboardFocusHere();
        }
        ImGui::InputText("##field", buffer.data(), buffer.size());
        ImGui::End();
    };
    // Focus is requested in the first frame, the field activates in the second, ImGui reports it
    // from the third (its want-flags describe the previous frame).
    for (int i = 0; i < 3; ++i)
    {
        frame(ui, input, field);
    }
    CHECK(ui.wantsText());
    CHECK(ui.wantsKeyboard());
    input.onText("Gr\xC3\xBC\xC3\x9F"
                 "e");
    frame(ui, input, field);
    frame(ui, input, field);
    CHECK(std::strcmp(buffer.data(), "Gr\xC3\xBC\xC3\x9F"
                                     "e") == 0);
}

TEST_CASE("DebugUi: the engine panel keeps values it does not edit")
{
    DebugUi ui = makeUi();
    Input input;
    ui.beginFrame(input, kSize, kSize, 1.0f / 60.0f);
    EnginePanel panel;
    panel.fovDegrees = 85.0f;
    panel.exposure = 1.5f;
    panel.fogDensity = 0.004f;
    panel.tonemapper = render::Tonemapper::Reinhard;
    panel.paused = true;
    panel.timeScale = 0.25f;
    const EnginePanel before = panel;
    ui.enginePanel(panel);
    ui.endFrame(nullptr);
    CHECK(panel.fovDegrees == before.fovDegrees);
    CHECK(panel.exposure == before.exposure);
    CHECK(panel.fogDensity == before.fogDensity);
    CHECK(panel.tonemapper == before.tonemapper);
    CHECK(panel.paused == before.paused);
    CHECK(panel.timeScale == before.timeScale);
}
