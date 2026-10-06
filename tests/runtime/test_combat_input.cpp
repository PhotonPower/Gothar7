// The hero's fighting keys (M11 part B, K1) with the bindings of game/config/engine.toml: Gothic 1 - the
// action key held plus a direction -, the mouse as second assignment.

#include <g7/core/Config.hpp>
#include <g7/core/FileSystem.hpp>
#include <g7/runtime/CombatInput.hpp>

#include <doctest/doctest.h>

#include <ostream> // doctest needs it to print std::string operands
#include <string>

using namespace g7;
using platform::Key;
using platform::MouseButton;

namespace
{
platform::ActionMap classic()
{
    const auto text = fs::readText(fs::fromUtf8(G7_GAME_CONFIG));
    REQUIRE(text.ok());
    auto config = Config::parse(text.value(), "engine.toml");
    REQUIRE(config.ok());
    return platform::ActionMap::fromConfig(config.value(), "classic");
}

std::string asked(const runtime::CombatInput& in)
{
    if (!in.move)
    {
        return "-";
    }
    const char* kinds[] = {"front", "left", "right"};
    return in.move->first + (in.move->first == "attack"
                                 ? std::string(" ") + kinds[static_cast<int>(in.move->second)]
                                 : std::string());
}
} // namespace

TEST_CASE("Combat keys: Ctrl + direction like Gothic 1, the mouse as second assignment")
{
    const platform::ActionMap map = classic();
    platform::Input input;
    const auto frame = [&](auto&& feed)
    {
        input.beginFrame();
        feed();
        return runtime::combatInput(map, input);
    };
    // Ctrl held, then up: a blow; the hero stands while Ctrl is held.
    auto in = frame([&] { input.onKey(Key::LeftCtrl, true); });
    CHECK(in.holdsAction);
    CHECK(asked(in) == "-");
    CHECK(asked(frame([&] { input.onKey(Key::Up, true); })) == "attack front");
    CHECK(asked(frame([&] {})) == "-"); // held, not pressed again
    CHECK(asked(frame([&] { input.onKey(Key::Up, false); })) == "-");
    CHECK(asked(frame([&] { input.onKey(Key::W, true); })) == "attack front"); // again: the next combo hit
    CHECK(asked(frame([&] { input.onKey(Key::Left, true); })) == "attack left");
    CHECK(asked(frame([&] { input.onKey(Key::D, true); })) == "attack right");
    CHECK(asked(frame([&] { input.onKey(Key::Down, true); })) == "parry");
    CHECK(asked(frame([&] { input.onKey(Key::LeftAlt, true); })) == "dodge");
    // Without Ctrl the arrows walk: nothing asked for.
    frame(
        [&]
        {
            for (const Key k : {Key::LeftCtrl, Key::W, Key::Left, Key::D, Key::Down, Key::LeftAlt})
            {
                input.onKey(k, false);
            }
        });
    in = frame(
        [&]
        {
            input.onKey(Key::Up, false);
            input.onKey(Key::Up, true);
        });
    CHECK_FALSE(in.holdsAction);
    CHECK(asked(in) == "-");
    // The mouse: left strikes (with left held: to the side), right parries.
    CHECK(asked(frame([&] { input.onMouseButton(MouseButton::Left, true); })) == "attack front");
    frame([&] { input.onMouseButton(MouseButton::Left, false); });
    CHECK(asked(frame(
              [&]
              {
                  input.onKey(Key::Up, false);
                  input.onKey(Key::Left, true);
                  input.onMouseButton(MouseButton::Left, true);
              })) == "attack left");
    CHECK(asked(frame([&] { input.onMouseButton(MouseButton::Right, true); })) == "parry");
}
