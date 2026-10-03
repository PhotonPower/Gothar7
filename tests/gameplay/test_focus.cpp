// Focus selection (M8 part B): cone and range per kind, priority NPC > Mob > Item, hysteresis, visibility,
// settings from TOML.

#include <g7/gameplay/Focus.hpp>

#include <doctest/doctest.h>

#include <ostream> // doctest needs it to print std::string operands
#include <string>
#include <vector>

using namespace g7;
using namespace g7::gameplay;

namespace
{
const Vec3 kEye(0.0f, 1.7f, 0.0f);
const Vec3 kAhead(0.0f, 0.0f, -1.0f);

/// A point `distance` metres away, `degrees` to the right of straight ahead, at eye height.
Vec3 at(f32 distance, f32 degrees)
{
    const f32 r = degrees / 57.2957795f;
    return kEye + Vec3(std::sin(r), 0.0f, -std::cos(r)) * distance;
}
} // namespace

TEST_CASE("Focus: within cone and range, the closest to the view direction wins")
{
    const FocusSettings settings; // item: 2.5 m, 35 degrees
    std::vector<FocusCandidate> items = {{1, FocusKind::Item, at(2.0f, 20.0f)},
                                         {2, FocusKind::Item, at(2.0f, 5.0f)},
                                         {3, FocusKind::Item, at(3.0f, 0.0f)},   // too far
                                         {4, FocusKind::Item, at(1.0f, 50.0f)}}; // outside the cone
    CHECK(selectFocus(items, kEye, kAhead, settings, std::nullopt) == 2u);
    // Nothing in range.
    CHECK_FALSE(selectFocus(std::span(items).subspan(2), kEye, kAhead, settings, std::nullopt).has_value());
    // No direction: nothing.
    CHECK_FALSE(selectFocus(items, kEye, Vec3(0.0f), settings, std::nullopt).has_value());
    // An item on the floor 1.5 m ahead (45 degrees below the eye) is ahead; one on a roof 3 m up is not.
    const std::vector<FocusCandidate> floor = {{7, FocusKind::Item, Vec3(0.2f, 0.0f, -1.5f)},
                                               {8, FocusKind::Item, Vec3(0.0f, 1.7f + 2.4f, -1.0f)}};
    CHECK(selectFocus(floor, kEye, kAhead, settings, std::nullopt) == 7u);
    CHECK_FALSE(selectFocus(std::span(floor).subspan(1), kEye, kAhead, settings, std::nullopt).has_value());
}

TEST_CASE("Focus: NPC before mob before item, whatever the angle")
{
    const FocusSettings settings;
    const std::vector<FocusCandidate> all = {{1, FocusKind::Item, at(1.0f, 0.0f)},
                                             {2, FocusKind::Mob, at(2.0f, 10.0f)},
                                             {3, FocusKind::Npc, at(6.0f, 25.0f)}};
    CHECK(selectFocus(all, kEye, kAhead, settings, std::nullopt) == 3u);
    CHECK(selectFocus(std::span(all).first(2), kEye, kAhead, settings, std::nullopt) == 2u);
    // The NPC's longer range: 6 m is fine for an NPC, not for a mob.
    const std::vector<FocusCandidate> far = {{5, FocusKind::Mob, at(6.0f, 0.0f)}};
    CHECK_FALSE(selectFocus(far, kEye, kAhead, settings, std::nullopt).has_value());
}

TEST_CASE("Focus: hysteresis keeps the current one, a higher kind still takes over")
{
    const FocusSettings settings;
    std::vector<FocusCandidate> items = {{1, FocusKind::Item, at(2.0f, 10.0f)},
                                         {2, FocusKind::Item, at(2.0f, 12.0f)}};
    // Item 2 is focused; item 1 is slightly better but the focus stays.
    CHECK(selectFocus(items, kEye, kAhead, settings, 2u) == 2u);
    // Just outside the item cone (38 degrees) but inside 1.25 times it: kept.
    items[1].point = at(2.0f, 38.0f);
    CHECK(selectFocus(items, kEye, kAhead, settings, 2u) == 2u);
    // Beyond the widened cone: the better one takes over.
    items[1].point = at(2.0f, 46.0f);
    CHECK(selectFocus(items, kEye, kAhead, settings, 2u) == 1u);
    // An NPC coming into range replaces a kept item.
    items[1].point = at(2.0f, 12.0f);
    items.push_back({9, FocusKind::Npc, at(5.0f, 20.0f)});
    CHECK(selectFocus(items, kEye, kAhead, settings, 2u) == 9u);
    // A focus id that is gone no longer counts.
    CHECK(selectFocus(std::span(items).first(2), kEye, kAhead, settings, 77u) == 1u);
}

TEST_CASE("Focus: hidden candidates are skipped")
{
    const FocusSettings settings;
    const std::vector<FocusCandidate> items = {{1, FocusKind::Item, at(2.0f, 2.0f)},
                                               {2, FocusKind::Item, at(2.0f, 20.0f)}};
    const FocusVisible notOne = [](const FocusCandidate& c) { return c.id != 1; };
    CHECK(selectFocus(items, kEye, kAhead, settings, std::nullopt, notOne) == 2u);
    CHECK(selectFocus(items, kEye, kAhead, settings, 1u, notOne) == 2u);
}

TEST_CASE("Focus: settings from TOML")
{
    auto parsed =
        FocusSettings::parse("keep = 1.5\n[item]\ndistance = 1.8\n[npc]\nangle = 20\n", "focus.toml");
    REQUIRE(parsed.ok());
    CHECK(parsed.value().keep == 1.5f);
    CHECK(parsed.value().range(FocusKind::Item).distance == 1.8f);
    CHECK(parsed.value().range(FocusKind::Item).angle == 35.0f); // default kept
    CHECK(parsed.value().range(FocusKind::Npc).angle == 20.0f);
    const auto bad = FocusSettings::parse("[mob]\nangle = 95\n", "focus.toml");
    REQUIRE_FALSE(bad.ok());
    CHECK(bad.error().message == "focus.toml: 'mob.angle' must be below 90 degrees");
    CHECK_FALSE(FocusSettings::parse("keep = 0.5\n", "focus.toml").ok());
    CHECK_FALSE(FocusSettings::parse("[item]\ndistance = -1\n", "focus.toml").ok());
    CHECK(focusKindName(FocusKind::Mob) == "mob");
}
