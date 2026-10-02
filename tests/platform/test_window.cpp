#include <g7/platform/Window.hpp>

#include <doctest/doctest.h>

#include <memory>

using namespace g7;
using namespace g7::platform;

namespace
{
std::unique_ptr<Window> makeWindow(WindowDesc desc = {})
{
    desc.title = "g7 platform test";
    desc.size = {640, 480};
    auto window = Window::create(desc);
    REQUIRE_MESSAGE(window.ok(), (window.ok() ? "" : window.error().message));
    return std::move(window).value();
}
} // namespace

TEST_CASE("Window: created with requested size and title")
{
    auto window = makeWindow();
    CHECK(window->size() == Extent{640, 480});
    CHECK(window->pixelSize().width >= 640);
    CHECK(window->mode() == WindowMode::Windowed);
    CHECK(window->title() == "g7 platform test");
    CHECK(window->pollEvents());
    CHECK_FALSE(window->resizedSinceLastPoll());
}

TEST_CASE("Window: invalid size is rejected")
{
    WindowDesc desc;
    desc.size = {0, 480};
    auto window = Window::create(desc);
    REQUIRE_FALSE(window.ok());
    CHECK_FALSE(window.error().message.empty());
}

TEST_CASE("Window: setSize is reported after the next poll")
{
    auto window = makeWindow();
    window->setSize({800, 600});
    CHECK(window->pollEvents());
    CHECK(window->size() == Extent{800, 600});
    CHECK(window->resizedSinceLastPoll());

    // Flag only covers the last poll.
    CHECK(window->pollEvents());
    CHECK_FALSE(window->resizedSinceLastPoll());

    // Invalid sizes are ignored.
    window->setSize({0, 0});
    CHECK(window->pollEvents());
    CHECK(window->size() == Extent{800, 600});
}

TEST_CASE("Window: toggle fullscreen and back")
{
    auto window = makeWindow();
    window->setMode(WindowMode::Fullscreen);
    CHECK(window->mode() == WindowMode::Fullscreen);
    CHECK(window->pollEvents());

    window->setMode(WindowMode::Windowed);
    CHECK(window->mode() == WindowMode::Windowed);
    CHECK(window->pollEvents());
}

TEST_CASE("Window: created in fullscreen mode")
{
    WindowDesc desc;
    desc.mode = WindowMode::Fullscreen;
    auto window = makeWindow(desc);
    CHECK(window->mode() == WindowMode::Fullscreen);
}

TEST_CASE("Window: title can be changed")
{
    auto window = makeWindow();
    window->setTitle("Gothar \xE2\x80\x93 Alter Lager"); // UTF-8 en dash
    CHECK(window->title() == "Gothar \xE2\x80\x93 Alter Lager");
}

TEST_CASE("Window: requestClose ends polling")
{
    auto window = makeWindow();
    CHECK(window->pollEvents());
    window->requestClose();
    CHECK_FALSE(window->pollEvents());
    CHECK_FALSE(window->pollEvents()); // stays closed
}

TEST_CASE("Window: two windows can coexist")
{
    auto first = makeWindow();
    {
        auto second = makeWindow();
        CHECK(second->pollEvents());
    }
    // Destroying the second window must not shut down video for the first.
    CHECK(first->pollEvents());
    first->setSize({320, 240});
    CHECK(first->pollEvents());
    CHECK(first->size() == Extent{320, 240});
}
