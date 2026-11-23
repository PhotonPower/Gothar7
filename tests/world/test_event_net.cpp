// The event net (docs/modules/dungeon.md D1): trigger targets and relay vobs in .g7world, and the relay logic
// (any, all, count, delay, invert, once, cycles, save state).

#include <g7/world/EventNet.hpp>
#include <g7/world/Scene.hpp>
#include <g7/world/WorldFile.hpp>
#include <g7/world/WorldScene.hpp>

#include <doctest/doctest.h>

#include <ostream>
#include <string>
#include <vector>

using namespace g7;
using namespace g7::world;

namespace
{
WorldFile parse(std::string_view text)
{
    auto world = parseWorldFile(text, "net.g7world");
    REQUIRE_MESSAGE(world.ok(), (world.ok() ? "" : world.error().message));
    return std::move(world).value();
}

std::string errorOf(std::string_view vob)
{
    auto world =
        parseWorldFile(std::string(R"({"version": 1, "vobs": [)") + std::string(vob) + "]}", "w.g7world");
    REQUIRE_FALSE(world.ok());
    return world.error().message;
}

/// A net whose deliveries are written down as "target action source".
struct Net
{
    EventNet net;
    std::vector<std::string> got;

    Net()
    {
        net.setDeliver([this](const NetEvent& e)
                       { got.push_back(e.target + " " + e.action + " " + e.source); });
    }
    void relay(std::string name, Relay r) { net.addRelay(std::move(name), r); }
    void wire(std::string_view sender, std::vector<std::string> targets) { net.addSender(sender, targets); }
    std::vector<std::string> step(f32 seconds = 1.0f / 60.0f)
    {
        got.clear();
        net.update(seconds);
        return got;
    }
};

Relay relayTo(std::vector<std::string> targets, std::string action, Relay::Mode mode = Relay::Mode::Any)
{
    Relay r;
    r.mode = mode;
    r.targets = std::move(targets);
    r.action = std::move(action);
    return r;
}

using Got = std::vector<std::string>;
} // namespace

TEST_CASE(
    "Event net format: trigger targets and relay vobs are read, spawned, captured and written unchanged")
{
    const WorldFile file = parse(R"({"version": 1, "name": "net", "vobs": [
  {"id": 1, "type": "trigger", "name": "PLATE_A", "components": {"trigger": {"targets": ["RELAY_GATE"], "action": "on"}}},
  {"id": 2, "type": "relay", "name": "RELAY_GATE", "pos": [1, 0, 0],
   "components": {"relay": {"mode": "count", "count": 2, "delay": 1.5, "once": true, "invert": true,
                            "targets": ["GATE_E01_1_2", "lua_room"], "action": "open"}}},
  {"id": 3, "type": "relay", "name": "RELAY_PLAIN"}
]})");
    REQUIRE(file.vobs.size() == 3);
    CHECK(file.vobs[0].trigger.targets == std::vector<std::string>{"RELAY_GATE"});
    CHECK(file.vobs[0].trigger.action == "on");
    REQUIRE(file.vobs[1].type == VobType::Relay);
    const Relay& r = file.vobs[1].relay;
    CHECK(r.mode == Relay::Mode::Count);
    CHECK(r.count == 2);
    CHECK(r.delay == 1.5f);
    CHECK(r.once);
    CHECK(r.invert);
    CHECK(r.targets == std::vector<std::string>{"GATE_E01_1_2", "lua_room"});
    CHECK(r.action == "open");
    const Relay& plain = file.vobs[2].relay; // the defaults
    CHECK(plain.mode == Relay::Mode::Any);
    CHECK(plain.action == "trigger");
    CHECK(plain.targets.empty());

    Scene scene;
    REQUIRE(spawnWorld(scene, file).ok());
    CHECK(scene.get<Relay>(scene.findById(VobId{2}))->count == 2);
    const std::string written = writeWorldFile(captureWorld(scene, "net"));
    CHECK(writeWorldFile(parse(written)) == written);
    CHECK(written.find(
              R"("components":{"trigger":{"shape":"box","halfExtents":[1.0,1.0,1.0],"filter":"player",)"
              R"("once":false,"targets":["RELAY_GATE"],"action":"on"}})") != std::string::npos);
    CHECK(written.find(
              R"("components":{"relay":{"mode":"count","count":2,"delay":1.5,"once":true,"invert":true,)"
              R"("targets":["GATE_E01_1_2","lua_room"],"action":"open"}})") != std::string::npos);
    CHECK(written.find(R"({"relay":{"mode":"any","delay":0.0,"once":false,"invert":false,"targets":[],)"
                       R"("action":"trigger"}})") != std::string::npos);
    // A trigger without targets writes neither (worlds without the net stay byte-equal).
    const WorldFile none = parse(R"({"version": 1, "vobs": [{"id": 1, "type": "trigger"}]})");
    CHECK(writeWorldFile(none).find("action") == std::string::npos);
}

TEST_CASE("Event net format: broken targets, actions and relays are errors naming the entry")
{
    CHECK(errorOf(R"({"id": 1, "type": "trigger", "components": {"trigger": {"targets": "GATE"}}})") ==
          "w.g7world: vobs[0].components.trigger: 'targets' must be a list of vob names");
    CHECK(errorOf(R"({"id": 1, "type": "trigger", "components": {"trigger": {"targets": [""]}}})") ==
          "w.g7world: vobs[0].components.trigger: 'targets' must be a list of vob names");
    CHECK(errorOf(R"({"id": 1, "type": "trigger", "components": {"trigger": {"action": "explode"}}})") ==
          "w.g7world: vobs[0].components.trigger: 'action' must be one of 'toggle', 'open', 'close', 'on', "
          "'off', 'wake', 'trigger', 'lock', 'unlock'");
    CHECK(errorOf(R"({"id": 1, "type": "relay", "components": {"relay": {"mode": "most"}}})") ==
          "w.g7world: vobs[0].components.relay: 'mode' must be one of 'any', 'all', 'count'");
    CHECK(errorOf(R"({"id": 1, "type": "relay", "components": {"relay": {"count": 0}}})") ==
          "w.g7world: vobs[0].components.relay.count: must be a whole number from 1 to 1000000");
    CHECK(errorOf(R"({"id": 1, "type": "relay", "components": {"relay": {"delay": -1}}})") ==
          "w.g7world: vobs[0].components.relay: 'delay' must be 0 or more seconds");
    CHECK(errorOf(R"({"id": 1, "type": "relay", "components": {"relay": {"invert": 1}}})") ==
          "w.g7world: vobs[0].components.relay.invert: must be true or false");
}

TEST_CASE("Event net: events for other vobs are delivered in order, relays of mode any pass them on")
{
    Net n;
    n.relay("R", relayTo({"GATE", "DOOR"}, "open"));
    n.net.send("LUA_ROOM", "wake", "PLATE");
    n.net.send("R", "trigger", "PLATE");
    CHECK(n.step() == Got{"LUA_ROOM wake PLATE", "GATE open R", "DOOR open R"});
    // An "off" event goes on as the opposite action (a plate released).
    n.net.send("R", "off", "PLATE");
    CHECK(n.step() == Got{"GATE close R", "DOOR close R"});
    CHECK(n.step().empty());
}

TEST_CASE("Event net: all waits for every wired input, the opposite once one goes off")
{
    Net n;
    n.relay("R", relayTo({"GATE"}, "open", Relay::Mode::All));
    n.wire("PLATE_A", {"R"});
    n.wire("PLATE_B", {"R"});
    n.net.send("R", "on", "PLATE_A");
    CHECK(n.step().empty());
    CHECK(n.net.relayState("R").inputs == std::vector<std::string>{"PLATE_A"});
    n.net.send("R", "on", "PLATE_B");
    CHECK(n.step() == Got{"GATE open R"});
    n.net.send("R", "on", "PLATE_B"); // already on: nothing changes
    CHECK(n.step().empty());
    n.net.send("R", "off", "PLATE_A");
    CHECK(n.step() == Got{"GATE close R"});
    n.net.send("R", "toggle", "PLATE_A"); // toggle flips the input
    CHECK(n.step() == Got{"GATE open R"});
}

TEST_CASE("Event net: count fires after n events and counts anew; once fires one time only")
{
    Net n;
    Relay counted = relayTo({"CHEST"}, "unlock", Relay::Mode::Count);
    counted.count = 3;
    n.relay("R", counted);
    for (int i = 0; i < 2; ++i)
    {
        n.net.send("R", "trigger", "BUTTON");
        CHECK(n.step().empty());
    }
    n.net.send("R", "off", "BUTTON"); // the off side does not count
    CHECK(n.step().empty());
    CHECK(n.net.relayState("R").counted == 2);
    n.net.send("R", "trigger", "BUTTON");
    CHECK(n.step() == Got{"CHEST unlock R"});
    CHECK(n.net.relayState("R").counted == 0);

    Relay once = relayTo({"GATE"}, "toggle");
    once.once = true;
    n.relay("ONCE", once);
    n.net.send("ONCE", "trigger", "LEVER");
    CHECK(n.step() == Got{"GATE toggle ONCE"});
    CHECK(n.net.relayState("ONCE").spent);
    n.net.send("ONCE", "trigger", "LEVER");
    CHECK(n.step().empty());
}

TEST_CASE("Event net: delay holds what a relay sends, invert sends the opposite")
{
    Net n;
    Relay delayed = relayTo({"GATE"}, "close");
    delayed.delay = 0.5f;
    delayed.invert = true; // a plate holds the gate open: "close" becomes "open"
    n.relay("R", delayed);
    n.net.send("R", "on", "PLATE");
    CHECK(n.step(0.25f).empty()); // the relay fires in this step; its event arrives 0.5 s later
    CHECK(n.net.pendingCount() == 1);
    CHECK(n.step(0.25f).empty());
    CHECK(n.step(0.25f) == Got{"GATE open R"});
    n.net.send("R", "off", "PLATE");
    CHECK(n.step(0.25f).empty());
    CHECK(n.step(0.25f).empty());
    CHECK(n.step(0.25f) == Got{"GATE close R"});
}

TEST_CASE("Event net: a cycle is reported and runs one round per step; relays chain within a step")
{
    Net n;
    n.relay("A", relayTo({"B", "LAMP"}, "toggle"));
    n.relay("B", relayTo({"A"}, "toggle"));
    n.relay("C", relayTo({"D"}, "on"));
    n.relay("D", relayTo({"GATE"}, "open"));
    CHECK(n.net.cycles() == std::vector<std::string>{"A", "B"});
    n.net.send("C", "trigger", "PLATE");
    CHECK(n.step() == Got{"GATE open D"}); // C -> D -> GATE in the same step
    n.net.send("A", "trigger", "LEVER");
    CHECK(n.step() == Got{"LAMP toggle A"}); // A, B, then A again waits for the next step
    CHECK(n.step() == Got{"LAMP toggle A"});
    CHECK(n.step() == Got{"LAMP toggle A"});
}

TEST_CASE("Event net: a delivery that sends again is capped per step")
{
    EventNet net;
    usize delivered = 0;
    net.setDeliver(
        [&](const NetEvent& e)
        {
            ++delivered;
            net.send(e.target, e.action, e.source); // a script answering itself forever
        });
    net.send("ECHO", "trigger", "lua");
    net.update(1.0f / 60.0f);
    CHECK(delivered == EventNet::kMaxEventsPerStep);
    CHECK(net.pendingCount() == 1);
}

TEST_CASE("Event net: snapshot and restore keep counters, inputs, spent relays and events on their way")
{
    Net n;
    Relay all = relayTo({"GATE"}, "open", Relay::Mode::All);
    Relay count = relayTo({"GATE"}, "toggle", Relay::Mode::Count);
    count.count = 5;
    Relay once = relayTo({"LAMP"}, "on");
    once.once = true;
    Relay slow = relayTo({"DOOR"}, "open");
    slow.delay = 2.0f;
    n.relay("ALL", all);
    n.relay("COUNT", count);
    n.relay("ONCE", once);
    n.relay("SLOW", slow);
    n.wire("PLATE_A", {"ALL"});
    n.wire("PLATE_B", {"ALL"});
    n.net.send("ALL", "on", "PLATE_B");
    n.net.send("COUNT", "trigger", "BUTTON");
    n.net.send("COUNT", "trigger", "BUTTON");
    n.net.send("ONCE", "trigger", "LEVER");
    n.net.send("SLOW", "trigger", "LEVER");
    CHECK(n.step(0.5f) == Got{"LAMP on ONCE"});
    n.net.send("SOMEWHERE", "wake", "lua"); // not handled yet when saved
    const EventNetSnapshot saved = n.net.snapshot();
    REQUIRE(saved.relays.size() == 3); // SLOW is in its initial state
    CHECK(saved.relays[0] == RelaySnapshot{"ALL", 0, {"PLATE_B"}, false});
    CHECK(saved.relays[1] == RelaySnapshot{"COUNT", 2, {}, false});
    CHECK(saved.relays[2] == RelaySnapshot{"ONCE", 0, {}, true});
    REQUIRE(saved.pending.size() == 2);
    CHECK(saved.pending[0] == PendingNetEvent{{"SOMEWHERE", "wake", "lua"}, 0.0f});
    CHECK(saved.pending[1] == PendingNetEvent{{"DOOR", "open", "SLOW"}, 2.0f});

    Net loaded;
    loaded.relay("ALL", all);
    loaded.relay("COUNT", count);
    loaded.relay("ONCE", once);
    loaded.relay("SLOW", slow);
    loaded.wire("PLATE_A", {"ALL"});
    loaded.wire("PLATE_B", {"ALL"});
    CHECK(loaded.net.restore(saved).empty());
    CHECK(loaded.net.snapshot() == saved);
    CHECK(loaded.step(1.0f) == Got{"SOMEWHERE wake lua"});
    CHECK(loaded.step(1.0f) == Got{"DOOR open SLOW"});
    loaded.net.send("ALL", "on", "PLATE_A");
    CHECK(loaded.step() == Got{"GATE open ALL"});
    loaded.net.send("ONCE", "trigger", "LEVER");
    CHECK(loaded.step().empty());

    // A relay gone from the world is reported, the rest put back.
    EventNetSnapshot extra = saved;
    extra.relays.push_back({"GONE", 1, {}, false});
    Net other;
    other.relay("ALL", all);
    CHECK(other.net.restore(extra) == std::vector<std::string>{"COUNT", "ONCE", "GONE"});
}
