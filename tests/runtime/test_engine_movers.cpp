// Moving parts in the engine (docs/modules/dungeon.md D2) on testworld/movers.g7world: a trigger opens a gate
// whose drawing and collision move with it; a gate closing on the hero stops and opens again (a trap gate
// hurts him first); a trapdoor turns down; the waynet edges through a closed gate or an open trapdoor are
// blocked; where they stand survives a save game.

#include <g7/runtime/Engine.hpp>
#include <g7/save/GameState.hpp>

#include <doctest/doctest.h>

#include <format>
#include <ostream> // doctest needs it to print std::string operands
#include <string>

using namespace g7;

namespace
{
EngineConfig moversConfig()
{
    EngineConfig config;
    config.appName = "movers";
    config.headless = true;
    config.world = fs::fromUtf8("testworld/movers.g7world");
    config.start = "START_MOVER";
    config.startTime = "12:00";
    config.fixedFrameSeconds = 1.0 / 60.0;
    return config;
}

script::Value run(Engine& engine, std::string_view line)
{
    auto result = engine.runConsoleLine(line);
    REQUIRE_MESSAGE(result.ok(), std::string(line), ": ", (result.ok() ? "" : result.error().message));
    return result.value();
}

void frames(Engine& engine, int n)
{
    for (int i = 0; i < n; ++i)
    {
        REQUIRE(engine.runFrame());
    }
}

MoverInfo mover(const Engine& engine, std::string_view name)
{
    const auto info = engine.moverInfo(name);
    REQUIRE(info.has_value());
    return *info;
}

Vec3 vobPosition(Engine& engine, std::string_view name)
{
    const auto e = engine.scene().findByName(StringId(name));
    REQUIRE(e != entt::entity{entt::null});
    return Vec3(engine.scene().worldMatrix(e)[3]);
}

/// The way points of the waynet's path between two named points.
usize pathLength(const Engine& engine, std::string_view from, std::string_view to)
{
    const ai::Waynet& net = engine.waynet();
    const auto path = net.path(*net.find(from), *net.find(to));
    return path ? path->size() : 0;
}

/// What a ray at hip height from the middle towards +Z hits first (vob id, 0: nothing).
u64 rayNorth(const Engine& engine, f32 x)
{
    const auto hit = engine.physics().raycast(Vec3(x, 1.0f, 0.0f), Vec3(0.0f, 0.0f, 1.0f), 9.0f);
    return hit ? hit->userData : 0;
}

i64 heroHp(Engine& engine)
{
    return run(engine, "stat('hp')").asInteger();
}
} // namespace

TEST_CASE("Engine movers: a trigger opens a gate, its collision and the waynet follow")
{
    Engine engine(moversConfig());
    auto result = engine.init();
    REQUIRE_MESSAGE(result.ok(), (result.ok() ? "" : result.error().message));
    frames(engine, 3);

    MoverInfo gate = mover(engine, "GATE_TEST");
    CHECK(gate.kind == world::Mover::Kind::Gate);
    CHECK_FALSE(gate.open);
    CHECK(gate.position == 0.0f);
    CHECK(gate.blocks);
    CHECK(rayNorth(engine, 0.0f) == 2);
    // The closed gate blocks the short way; the way round by the east stays (the trap gate there is open).
    CHECK(engine.waynet().blocked(*engine.waynet().find("WP_MOVER_MITTE"),
                                  *engine.waynet().find("WP_MOVER_HINTER_TOR")));
    CHECK(pathLength(engine, "WP_MOVER_MITTE", "WP_MOVER_HINTER_TOR") == 4);
    CHECK_FALSE(mover(engine, "GATE_FALLE").blocks);
    CHECK_FALSE(engine.moverInfo("NOTHING").has_value());
    CHECK(run(engine, "mover_state('NOTHING')").isNil());

    // The lever plate toggles it: it moves for a second, up by its height.
    run(engine, "teleport(5, 0, 0)");
    frames(engine, 3);
    gate = mover(engine, "GATE_TEST");
    CHECK(gate.open);
    CHECK(gate.moving);
    CHECK(run(engine, "mover_state('GATE_TEST').kind").asString() == "gate");
    CHECK(run(engine, "mover_state('GATE_TEST').moving").asBool());
    frames(engine, 65);
    gate = mover(engine, "GATE_TEST");
    CHECK(gate.position == 1.0f);
    CHECK_FALSE(gate.moving);
    CHECK_FALSE(gate.blocks);
    CHECK(vobPosition(engine, "GATE_TEST").y == doctest::Approx(3.0f));
    CHECK(rayNorth(engine, 0.0f) != 2); // the way through is free
    CHECK(pathLength(engine, "WP_MOVER_MITTE", "WP_MOVER_HINTER_TOR") == 2);
    CHECK(run(engine, "mover_state('GATE_TEST').position").asNumber() == doctest::Approx(1.0));

    // Lua closes it again; the secret wall slides aside by its offset.
    run(engine, "teleport(0, 0, -3)");
    run(engine, "send('GATE_TEST', 'close') send('WALL_TEST', 'open')");
    frames(engine, 200);
    CHECK_FALSE(mover(engine, "GATE_TEST").open);
    CHECK(mover(engine, "GATE_TEST").blocks);
    CHECK(rayNorth(engine, 0.0f) == 2);
    CHECK(vobPosition(engine, "WALL_TEST").x == doctest::Approx(-7.0f));
    CHECK(mover(engine, "WALL_TEST").position == 1.0f);

    // The trapdoor turns its leaf down and blocks the edge over its shaft while open.
    CHECK_FALSE(mover(engine, "TRAP_TEST").blocks);
    CHECK(pathLength(engine, "WP_MOVER_MITTE", "WP_MOVER_HINTER_KLAPPE") == 2);
    run(engine, "send('TRAP_TEST', 'on')");
    frames(engine, 45);
    CHECK(mover(engine, "TRAP_TEST").position == 1.0f);
    CHECK(mover(engine, "TRAP_TEST").blocks);
    CHECK(pathLength(engine, "WP_MOVER_MITTE", "WP_MOVER_HINTER_KLAPPE") == 0);
    const auto trap = engine.scene().findByName(StringId("TRAP_TEST"));
    const Mat4 leaf = engine.scene().worldMatrix(trap);
    CHECK(glm::normalize(Vec3(leaf[2])).y == doctest::Approx(-1.0f).epsilon(0.001)); // +Z points down
}

TEST_CASE("Engine movers: a gate closing on the hero stops and opens again; a trap gate hurts first")
{
    Engine engine(moversConfig());
    REQUIRE(engine.init().ok());
    frames(engine, 3);

    // The plain gate: open it, stand in the gateway, close it.
    run(engine, "send('GATE_TEST', 'open')");
    frames(engine, 70);
    run(engine, "teleport(0, 0, 5)");
    frames(engine, 3);
    const i64 hp = heroHp(engine);
    run(engine, "send('GATE_TEST', 'close')");
    frames(engine, 90);
    MoverInfo gate = mover(engine, "GATE_TEST");
    CHECK(gate.crushes == 1);
    CHECK(gate.open);
    CHECK(gate.position == 1.0f);
    CHECK(heroHp(engine) == hp); // no harm (question 2)

    // The trap gate (open from the start) hits him with 20 and opens again.
    run(engine, "teleport(10, 0, 5)");
    frames(engine, 3);
    run(engine, "send('GATE_FALLE', 'close')");
    frames(engine, 90);
    gate = mover(engine, "GATE_FALLE");
    CHECK(gate.crushes == 1);
    CHECK(gate.open);
    CHECK(heroHp(engine) == hp - 20);

    // Out of the way it shuts.
    run(engine, "teleport(10, 0, 0)");
    frames(engine, 3);
    run(engine, "send('GATE_FALLE', 'toggle')");
    frames(engine, 90);
    gate = mover(engine, "GATE_FALLE");
    CHECK_FALSE(gate.open);
    CHECK(gate.position == 0.0f);
    CHECK(gate.crushes == 1);
    CHECK(gate.blocks);
}

TEST_CASE("Engine movers: where they stand and where they go is saved and comes back")
{
    std::string saved;
    {
        Engine engine(moversConfig());
        REQUIRE(engine.init().ok());
        frames(engine, 3);
        run(engine, "send('GATE_TEST', 'open') send('GATE_FALLE', 'close')");
        frames(engine, 70);
        run(engine, "send('TRAP_TEST', 'open')");
        frames(engine, 10); // on its way
        auto state = engine.captureGame();
        REQUIRE_MESSAGE(state.ok(), (state.ok() ? "" : state.error().message));
        const save::WorldState& world = state.value().worlds.back();
        REQUIRE(world.movers.size() == 3); // the wall is as in the file
        CHECK(world.movers[0].vob == 2);
        CHECK(world.movers[0].position == 1.0f);
        CHECK(world.movers[0].open);
        CHECK(world.movers[1].vob == 3);
        CHECK_FALSE(world.movers[1].open);
        CHECK(world.movers[2].vob == 5);
        CHECK(world.movers[2].position > 0.0f);
        CHECK(world.movers[2].position < 1.0f);
        // The world as stored keeps the movers closed with their state (not where they stand).
        CHECK(world.delta.changed.empty());
        saved = save::toJson(state.value());
    }
    CHECK(saved.find("\"movers\"") != std::string::npos);

    Engine engine(moversConfig());
    REQUIRE(engine.init().ok());
    frames(engine, 3);
    auto state = save::fromJson(saved);
    REQUIRE_MESSAGE(state.ok(), (state.ok() ? "" : state.error().message));
    REQUIRE(engine.applyGame(state.value()).ok());
    CHECK(mover(engine, "GATE_TEST").position == 1.0f);
    CHECK_FALSE(mover(engine, "GATE_TEST").blocks);
    CHECK(vobPosition(engine, "GATE_TEST").y == doctest::Approx(3.0f));
    CHECK(pathLength(engine, "WP_MOVER_MITTE", "WP_MOVER_HINTER_TOR") == 2);
    CHECK(mover(engine, "GATE_FALLE").position == 0.0f);
    CHECK(mover(engine, "GATE_FALLE").blocks);
    const MoverInfo trap = mover(engine, "TRAP_TEST");
    CHECK(trap.open);
    CHECK(trap.moving);
    frames(engine, 40);
    CHECK(mover(engine, "TRAP_TEST").position == 1.0f);

    // A save without movers (older) loads the world's initial state.
    auto old = save::fromJson(saved);
    REQUIRE(old.ok());
    old.value().worlds.back().movers.clear();
    CHECK(save::toJson(old.value()).find("\"movers\"") == std::string::npos);
    REQUIRE(engine.applyGame(old.value()).ok());
    CHECK(mover(engine, "GATE_TEST").position == 0.0f);
    CHECK(mover(engine, "GATE_FALLE").position == 1.0f);
}
