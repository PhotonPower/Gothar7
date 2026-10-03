// End-to-end tests of the SDL -> Input translation. This suite is the only code outside the
// platform module that sees SDL; it injects synthetic events and a virtual gamepad.

#include <g7/platform/Input.hpp>
#include <g7/platform/Window.hpp>

#include <SDL3/SDL.h>
#include <doctest/doctest.h>

#include <memory>
#include <ostream>
#include <string>

using namespace g7;
using namespace g7::platform;

namespace
{
std::unique_ptr<Window> makeWindow()
{
    WindowDesc desc;
    desc.size = {320, 240};
    auto window = Window::create(desc);
    REQUIRE_MESSAGE(window.ok(), (window.ok() ? "" : window.error().message));
    return std::move(window).value();
}

void pushKey(SDL_Scancode scancode, bool down, bool repeat = false)
{
    SDL_Event event{};
    event.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
    event.key.scancode = scancode;
    event.key.down = down;
    event.key.repeat = repeat;
    REQUIRE(SDL_PushEvent(&event));
}

bool poll(Window& window, Input& input)
{
    input.beginFrame();
    return window.pollEvents(input);
}
} // namespace

TEST_CASE("SDL input: keyboard scancodes")
{
    auto window = makeWindow();
    Input input;
    poll(*window, input); // drain startup events

    pushKey(SDL_SCANCODE_W, true);
    pushKey(SDL_SCANCODE_LCTRL, true);
    pushKey(SDL_SCANCODE_0, true);
    pushKey(SDL_SCANCODE_KP_7, true);
    pushKey(SDL_SCANCODE_F12, true);
    pushKey(SDL_SCANCODE_GRAVE, true);
    CHECK(poll(*window, input));
    CHECK(input.pressed(Key::W));
    CHECK(input.pressed(Key::LeftCtrl));
    CHECK(input.pressed(Key::Num0));
    CHECK(input.pressed(Key::Keypad7));
    CHECK(input.pressed(Key::F12));
    CHECK(input.pressed(Key::Grave));

    // Auto-repeat events do not create new edges.
    pushKey(SDL_SCANCODE_W, true, true);
    CHECK(poll(*window, input));
    CHECK(input.isDown(Key::W));
    CHECK_FALSE(input.pressed(Key::W));

    pushKey(SDL_SCANCODE_W, false);
    CHECK(poll(*window, input));
    CHECK(input.released(Key::W));
}

TEST_CASE("SDL input: mouse buttons, motion and wheel")
{
    auto window = makeWindow();
    Input input;
    poll(*window, input);

    SDL_Event button{};
    button.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
    button.button.button = SDL_BUTTON_RIGHT;
    button.button.down = true;
    REQUIRE(SDL_PushEvent(&button));

    SDL_Event motion{};
    motion.type = SDL_EVENT_MOUSE_MOTION;
    motion.motion.x = 100.0f;
    motion.motion.y = 50.0f;
    motion.motion.xrel = 4.0f;
    motion.motion.yrel = -2.0f;
    REQUIRE(SDL_PushEvent(&motion));

    SDL_Event wheel{};
    wheel.type = SDL_EVENT_MOUSE_WHEEL;
    wheel.wheel.y = 1.0f;
    wheel.wheel.direction = SDL_MOUSEWHEEL_FLIPPED;
    REQUIRE(SDL_PushEvent(&wheel));

    CHECK(poll(*window, input));
    CHECK(input.pressed(MouseButton::Right));
    CHECK(input.mousePosition() == Vec2(100, 50));
    CHECK(input.mouseDelta() == Vec2(4, -2));
    CHECK(input.wheelDelta() == doctest::Approx(-1.0f));
}

TEST_CASE("SDL input: focus loss releases held keys")
{
    auto window = makeWindow();
    Input input;
    poll(*window, input);

    pushKey(SDL_SCANCODE_LALT, true);
    CHECK(poll(*window, input));
    REQUIRE(input.isDown(Key::LeftAlt));

    SDL_Event focus{};
    focus.type = SDL_EVENT_WINDOW_FOCUS_LOST;
    REQUIRE(SDL_PushEvent(&focus));
    CHECK(poll(*window, input));
    CHECK_FALSE(input.isDown(Key::LeftAlt));
    CHECK(input.released(Key::LeftAlt));
}

TEST_CASE("SDL input: virtual gamepad connect, buttons, axes, disconnect")
{
    // Joystick events reach unfocused windows only with this hint: the test window may sit behind others.
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
    auto window = makeWindow();
    Input input;
    poll(*window, input);
    REQUIRE_FALSE(input.gamepadConnected());

    SDL_VirtualJoystickDesc desc;
    SDL_INIT_INTERFACE(&desc);
    desc.type = SDL_JOYSTICK_TYPE_GAMEPAD;
    desc.naxes = SDL_GAMEPAD_AXIS_COUNT;
    desc.nbuttons = SDL_GAMEPAD_BUTTON_COUNT;
    desc.name = "g7 virtual gamepad";
    const SDL_JoystickID id = SDL_AttachVirtualJoystick(&desc);
    REQUIRE_MESSAGE(id != 0, SDL_GetError());
    SDL_Joystick* joystick = SDL_OpenJoystick(id);
    REQUIRE(joystick != nullptr);

    CHECK(poll(*window, input));
    CHECK(input.gamepadConnected());

    SDL_SetJoystickVirtualButton(joystick, SDL_GAMEPAD_BUTTON_SOUTH, true);
    SDL_SetJoystickVirtualAxis(joystick, SDL_GAMEPAD_AXIS_LEFTY, -32768); // pushed forward
    SDL_SetJoystickVirtualAxis(joystick, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, 32767);
    CHECK(poll(*window, input));
    CHECK(input.pressed(GamepadButton::South));
    CHECK(input.axis(GamepadAxis::LeftY) == doctest::Approx(1.0f)); // +Y up
    CHECK(input.axis(GamepadAxis::RightTrigger) == doctest::Approx(1.0f));

    SDL_CloseJoystick(joystick);
    SDL_DetachVirtualJoystick(id);
    CHECK(poll(*window, input));
    CHECK_FALSE(input.gamepadConnected());
    CHECK_FALSE(input.isDown(GamepadButton::South));
    CHECK(input.axis(GamepadAxis::LeftY) == 0.0f);
}

TEST_CASE("SDL input: relative mouse mode")
{
    auto window = makeWindow();
    CHECK_FALSE(window->relativeMouse());
    if (window->setRelativeMouse(true))
    {
        CHECK(window->relativeMouse());
        CHECK(window->setRelativeMouse(false));
        CHECK_FALSE(window->relativeMouse());
    }
    else
    {
        const char* driver = SDL_GetCurrentVideoDriver();
        const std::string note =
            "relative mouse mode unsupported by video driver '" + std::string(driver ? driver : "none") + "'";
        MESSAGE(note);
    }
}

TEST_CASE("SDL input: text input")
{
    auto window = makeWindow();
    Input input;
    poll(*window, input);

    CHECK_FALSE(window->textInput()); // off by default
    window->setTextInput(true);
    CHECK(window->textInput());

    SDL_Event event{};
    event.type = SDL_EVENT_TEXT_INPUT;
    event.text.windowID = SDL_GetWindowID(SDL_GetKeyboardFocus());
    event.text.text = "Gr\xC3\xBC\xC3\x9F";
    REQUIRE(SDL_PushEvent(&event)); // SDL copies the text
    SDL_Event second = event;
    second.text.text = "e!";
    REQUIRE(SDL_PushEvent(&second));
    CHECK(poll(*window, input));
    CHECK(input.text() == "Gr\xC3\xBC\xC3\x9F"
                          "e!"); // UTF-8, several events concatenated
    CHECK(poll(*window, input));
    CHECK(input.text().empty()); // per frame

    window->setTextInput(false);
    CHECK_FALSE(window->textInput());
    CHECK(window->displayScale() > 0.0f);
}
