// Placing NPCs (label "headless"): insert_npc at a way point inside a room puts them on its floor, not on the
// ceiling (welt's walkable houses; the ground probe starts just above the point).

#include <g7/runtime/Engine.hpp>

#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>

using namespace g7;

TEST_CASE("Engine NPCs: inserted at a way point in a room they stand on its floor, not on the ceiling")
{
    const std::filesystem::path world = std::filesystem::temp_directory_path() / "g7_spawn_room.g7world";
    {
        std::ofstream out(world, std::ios::binary);
        out << R"({"version":1,"name":"room","nextVobId":4,"vobs":[)"
            << R"({"id":1,"type":"mesh","name":"FLOOR","pos":[0.0,-0.75,0.0],"rot":[0.0,0.0,0.0,1.0],)"
            << R"("scale":[20.0,1.0,20.0],"mesh":"mobs/table.glb"},)"
            << R"({"id":2,"type":"mesh","name":"CEILING","pos":[0.0,2.6,0.0],"rot":[0.0,0.0,0.0,1.0],)"
            << R"("scale":[5.0,1.0,8.0],"mesh":"mobs/table.glb"},)"
            << R"({"id":3,"type":"start","name":"START","pos":[6.0,0.1,0.0],"rot":[0.0,0.0,0.0,1.0]}],)"
            << R"("waynet":{"points":[{"name":"WP_ROOM","pos":[0.0,0.0,0.0]},{"name":"WP_STREET","pos":[6.0,-0.2,0.0]}],)"
            << R"("edges":[["WP_ROOM","WP_STREET"]],"freepoints":[]}})";
    }
    EngineConfig config;
    config.appName = "spawn";
    config.headless = true;
    config.world = world;
    config.start = "START";
    config.fixedFrameSeconds = 1.0 / 60.0;
    Engine engine(std::move(config));
    REQUIRE(engine.init().ok());
    const auto run = [&](std::string_view line)
    {
        auto r = engine.runConsoleLine(line);
        REQUIRE_MESSAGE(r.ok(), (r.ok() ? "" : r.error().message));
        return r.value();
    };
    REQUIRE(run("insert_npc('npc_old_man', 'WP_ROOM')").isString());
    run("set_routine('npc_old_man', '') npc_clear('npc_old_man')");
    for (int i = 0; i < 30; ++i)
    {
        REQUIRE(engine.runFrame());
    }
    CHECK(run("npc_state('npc_old_man').y").asNumber() == doctest::Approx(0.0).epsilon(0.1));
    // A way point a little below the ground (a hollow of the terrain model) is lifted onto it.
    REQUIRE(run("insert_npc('npc_woodcutter', 'WP_STREET')").isString());
    run("set_routine('npc_woodcutter', '') npc_clear('npc_woodcutter')");
    for (int i = 0; i < 30; ++i)
    {
        REQUIRE(engine.runFrame());
    }
    CHECK(run("npc_state('npc_woodcutter').y").asNumber() == doctest::Approx(0.0).epsilon(0.1));
    std::filesystem::remove(world);
}
