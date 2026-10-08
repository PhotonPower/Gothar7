// The torch (owner decision, as Gothic 1; label "headless"): used it is lit (figuren's clips: in the left
// hand, flame and light at its tip, the hold pose), used again put away; a one-handed weapon goes with it, a
// two-handed one, a bow and magic put it away; dropped it lies burning until picked up.

#include <g7/runtime/Engine.hpp>

#include <doctest/doctest.h>

#include <ostream> // doctest needs it to print std::string operands
#include <string>

using namespace g7;

namespace
{
EngineConfig torchConfig()
{
    EngineConfig config;
    config.appName = "torch";
    config.headless = true;
    config.world = fs::fromUtf8("testworld/camp.g7world");
    config.start = "START_LAGER";
    config.startTime = "22:00";
    config.fixedFrameSeconds = 1.0 / 60.0;
    return config;
}

script::Value run(Engine& engine, std::string_view line)
{
    auto result = engine.runConsoleLine(line);
    const std::string what = std::string(line) + ": " + (result.ok() ? "" : result.error().message);
    REQUIRE_MESSAGE(result.ok(), what);
    return result.value();
}

void runSeconds(Engine& engine, f32 seconds)
{
    for (int i = 0; i < static_cast<int>(seconds * 60.0f); ++i)
    {
        REQUIRE(engine.runFrame());
    }
}
} // namespace

TEST_CASE("Engine torch: lit by using it, put away by using it again; flame and light at the hand")
{
    Engine engine(torchConfig());
    REQUIRE(engine.init().ok());
    run(engine, "give_item('it_torch')");
    runSeconds(engine, 0.3f);
    const usize before = engine.particles().emitterCount();
    run(engine, "use_item('it_torch')");
    REQUIRE(engine.runFrame());
    CHECK(engine.playerAnimationState() == "none_t_torch_light"); // figuren's clip
    CHECK_FALSE(run(engine, "return hero_torch_lit()").asBool());
    runSeconds(engine, 1.5f);
    CHECK(run(engine, "return hero_torch_lit()").asBool());
    CHECK(engine.playerHolds("socket_hand_l"));
    CHECK(engine.particles().emitterCount() == before + 1); // the flame (with its light)
    // Walking with it: still burning.
    runSeconds(engine, 2.0f);
    CHECK(run(engine, "return hero_torch_lit()").asBool());
    run(engine, "use_item('it_torch')"); // out and away
    REQUIRE(engine.runFrame());
    CHECK_FALSE(run(engine, "return hero_torch_lit()").asBool());
    CHECK_FALSE(engine.playerHolds("socket_hand_l"));
    CHECK(run(engine, "return item_count('it_torch')").asInteger() == 1); // not used up
}

TEST_CASE(
    "Engine torch: a one-handed weapon goes with it, a two-handed one puts it away; dropped it lies burning")
{
    Engine engine(torchConfig());
    REQUIRE(engine.init().ok());
    run(engine, "give_item('it_torch') give_item('it_sword_old') equip('it_sword_old')");
    run(engine, "hero_torch()");
    runSeconds(engine, 1.5f);
    REQUIRE(run(engine, "return hero_torch_lit()").asBool());
    CHECK(run(engine, "return draw_weapon()").asString() == "weapon");
    runSeconds(engine, 1.5f);
    CHECK(run(engine, "return hero_torch_lit()").asBool()); // sword right, torch left
    run(engine, "draw_weapon()");                           // away again: the torch stays in the left hand
    runSeconds(engine, 1.5f);
    CHECK(run(engine, "return hero_torch_lit()").asBool());
    CHECK(engine.playerHolds("socket_hand_l"));

    run(engine, "set_stat('str', 40) give_item('it_sword_2h') equip('it_sword_2h') draw_weapon()");
    runSeconds(engine, 0.2f);
    CHECK_FALSE(run(engine, "return hero_torch_lit()").asBool()); // both hands for the two-handed one
    run(engine, "draw_weapon()");
    runSeconds(engine, 1.5f);

    // Dropped: out of the bag, burning on the ground; picked up again, the flame goes out.
    run(engine, "hero_torch()");
    runSeconds(engine, 1.5f);
    REQUIRE(run(engine, "return hero_torch_lit()").asBool());
    const usize burning = engine.particles().emitterCount();
    run(engine, "hero_torch_drop()");
    runSeconds(engine, 1.0f);
    CHECK_FALSE(run(engine, "return hero_torch_lit()").asBool());
    CHECK(run(engine, "return item_count('it_torch')").asInteger() == 0);
    CHECK(engine.particles().emitterCount() >= burning); // the one on the ground
}
