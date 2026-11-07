// Editor mode end to end (label "gpu", M4 DoD: place vobs in the editor and save): on a copy of the
// camp world in a temporary folder, never on the repository's file.

#include "GlFixture.hpp"

#include <g7/editor/Editor.hpp>
#include <g7/editor/Operations.hpp>
#include <g7/runtime/Engine.hpp>
#include <g7/world/Scene.hpp>
#include <g7/world/WorldFile.hpp>
#include <g7/world/WorldScene.hpp>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <string>

using namespace g7;

namespace
{
struct WorldCopy
{
    fs::Path dir;
    fs::Path file;
    explicit WorldCopy(std::string_view extraHead = {})
    {
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        dir = std::filesystem::temp_directory_path() / ("g7_editor_" + std::to_string(stamp));
        std::filesystem::create_directories(dir);
        file = dir / "camp_copy.g7world";
        auto text = fs::readText(fs::fromUtf8(G7_TESTSCENE).parent_path().parent_path() / "testworld" /
                                 "camp.g7world");
        REQUIRE(text.ok());
        std::string content = text.value();
        if (!extraHead.empty())
        {
            content.insert(content.find("\"nextVobId\""), std::string(extraHead));
        }
        REQUIRE(fs::writeText(file, content).ok());
    }
    ~WorldCopy()
    {
        std::error_code ignored;
        std::filesystem::remove_all(dir, ignored);
    }
};

EngineConfig editorConfig(const fs::Path& world)
{
    EngineConfig config;
    config.appName = "editor";
    config.window.size = {320, 180};
    config.window.vsync = false;
    config.world = world;
    config.start = "START_LAGER";
    config.player = false; // as gothar --editor
    config.shaderDirectory = fs::fromUtf8(G7_SHADER_DIR);
    config.fixedFrameSeconds = 1.0 / 60.0;
    // Everything the editor or the game could write stays in the temporary copy (never the user's folder).
    config.saveDirectory = world.parent_path() / "saves";
    config.extraScriptDirectory = world.parent_path() / "scripts";
    g7::test::keepVideoAlive();
    return config;
}
} // namespace

TEST_CASE("Editor GPU: place, move, duplicate, delete and save a world; the first save keeps a .bak")
{
    WorldCopy copy;
    Engine engine(editorConfig(copy.file));
    REQUIRE(engine.init().ok());
    editor::Editor editor(engine);
    engine.addTool(editor);
    CHECK(engine.paused());
    CHECK(engine.runFrame()); // editor windows drawn (debug UI on)
    REQUIRE(engine.worldSourceFile().has_value());
    const usize instancesBefore = engine.instances().size();

    // Place a rock: a new mesh vob with the next id, drawn right away.
    const u64 nextId = engine.scene().nextVobId();
    auto rock = editor.place("testscene/nature/rock_largeA.glb");
    REQUIRE_MESSAGE(rock.ok(), (rock.ok() ? "" : rock.error().message));
    CHECK(rock.value() == world::VobId{nextId});
    CHECK(editor.selection() == rock.value());
    CHECK(engine.instances().size() == instancesBefore + 1);
    CHECK(editor.dirty());

    // Move it like a drag: the render instance follows.
    Transform moved;
    moved.position = Vec3(40.0f, 0.0f, 5.0f);
    editor.setSelectionTransform(moved);
    bool found = false;
    for (const SceneInstance& instance : engine.instances())
    {
        if (instance.vob == rock.value())
        {
            found = true;
            CHECK(instance.bounds.center().x == doctest::Approx(40.0f).epsilon(0.05));
        }
    }
    CHECK(found);
    auto twin = editor.duplicateSelection();
    REQUIRE(twin.ok());
    CHECK(engine.instances().size() == instancesBefore + 2);
    editor.deleteSelection(); // the twin
    CHECK(engine.instances().size() == instancesBefore + 1);
    CHECK(engine.runFrame());

    // Save: back into the copy; the previous version is kept once as .bak.
    const auto original = fs::readText(copy.file).value();
    REQUIRE(editor.save().ok());
    CHECK_FALSE(editor.dirty());
    fs::Path backup = copy.file;
    backup += ".bak";
    CHECK(fs::readText(backup).value() == original);
    auto saved = world::parseWorldFile(fs::readText(copy.file).value(), "camp_copy.g7world");
    REQUIRE(saved.ok());
    bool hasRock = false;
    for (const world::WorldFileVob& vob : saved.value().vobs)
    {
        hasRock = hasRock ||
                  (vob.id == rock.value() && vob.name == "ROCK_LARGEA" && vob.transform.position.x == 40.0f);
    }
    CHECK(hasRock);
    CHECK(saved.value().terrain.has_value());     // the terrain block of the world stays
    CHECK(saved.value().nextVobId == nextId + 2); // the deleted twin's id stays used

    // A second save in the session keeps the first backup.
    editor.select(world::VobId{68});
    Transform stool = editor::worldTransformOf(engine.scene(), engine.scene().findById(world::VobId{68}));
    stool.position.x += 1.0f;
    editor.setSelectionTransform(stool);
    REQUIRE(editor.save().ok());
    CHECK(fs::readText(backup).value() == original);
    CHECK(engine.renderDevice()->debugErrorCount() == 0);
}

TEST_CASE("Editor GPU: vobs of the world's generator warn; archives cannot be saved")
{
    WorldCopy copy(R"("generator": {"tool": "gothar-worldgen", "owned": [[1, 100]]},
  )");
    Engine engine(editorConfig(copy.file));
    REQUIRE(engine.init().ok());
    editor::Editor editor(engine);
    engine.addTool(editor);
    CHECK(editor.warningFor(world::VobId{68}).find("locked") != std::string::npos);
    CHECK(editor.warningFor(world::VobId{170}).empty());
    // Saving keeps the generator head as it was.
    REQUIRE(editor.save().ok());
    CHECK(fs::readText(copy.file).value().find(
              "\"generator\": {\"tool\":\"gothar-worldgen\",\"owned\":[[1,100]]}") != std::string::npos);
}

TEST_CASE(
    "Editor GPU: undo and redo of placing, moving and deleting (M16 part A); the saved version is known")
{
    WorldCopy copy;
    Engine engine(editorConfig(copy.file));
    REQUIRE(engine.init().ok());
    editor::Editor editor(engine);
    engine.addTool(editor);
    const usize instancesBefore = engine.instances().size();
    const std::string original = fs::readText(copy.file).value();

    auto rock = editor.place("testscene/nature/rock_largeA.glb");
    REQUIRE(rock.ok());
    Transform moved;
    moved.position = Vec3(40.0f, 0.0f, 5.0f);
    editor.setSelectionTransform(moved);
    editor.deleteSelection();
    CHECK(engine.instances().size() == instancesBefore);
    CHECK(editor.history().undoLabel().starts_with("delete ROCK_LARGEA"));

    REQUIRE(editor.undo().value()); // the rock is back where it was moved to
    CHECK(engine.instances().size() == instancesBefore + 1);
    const entt::entity e = engine.scene().findById(rock.value());
    REQUIRE(engine.scene().valid(e));
    CHECK(editor::worldTransformOf(engine.scene(), e).position.x == doctest::Approx(40.0f));
    REQUIRE(editor.undo().value()); // not moved
    REQUIRE(editor.undo().value()); // not placed
    CHECK(engine.instances().size() == instancesBefore);
    CHECK_FALSE(editor.dirty()); // as loaded
    // Saving now writes the loaded world again (only the id counter may stay raised: ids are never reused).
    REQUIRE(editor.redo().value());
    REQUIRE(editor.save().ok());
    CHECK_FALSE(editor.dirty());
    REQUIRE(editor.undo().value());
    CHECK(editor.dirty());
    REQUIRE(editor.redo().value());
    CHECK_FALSE(editor.dirty());
    CHECK(fs::readText(copy.file).value() != original);
    CHECK(engine.renderDevice()->debugErrorCount() == 0);
}

TEST_CASE("Editor GPU: editing a generated vob asks first; saving changed generated vobs asks too")
{
    WorldCopy copy(R"("generator": {"tool": "gothar-worldgen", "owned": [[1, 100]]},
  )");
    Engine engine(editorConfig(copy.file));
    REQUIRE(engine.init().ok());
    editor::Editor editor(engine);
    engine.addTool(editor);
    // The stool (68) is generated: moving it asks and does nothing yet.
    editor.select(world::VobId{68});
    const Transform before =
        editor::worldTransformOf(engine.scene(), engine.scene().findById(world::VobId{68}));
    Transform moved = before;
    moved.position.x += 2.0f;
    editor.setSelectionTransform(moved);
    CHECK(editor.confirmation().find("generated") != std::string::npos);
    CHECK(editor::worldTransformOf(engine.scene(), engine.scene().findById(world::VobId{68})).position.x ==
          doctest::Approx(before.position.x));
    CHECK_FALSE(editor.history().canUndo());
    // No: still nothing. Asked again, yes: done, and later edits of generated vobs go without a question.
    editor.confirm(false);
    CHECK(editor.confirmation().empty());
    editor.setSelectionTransform(moved);
    editor.confirm(true);
    CHECK(editor::worldTransformOf(engine.scene(), engine.scene().findById(world::VobId{68})).position.x ==
          doctest::Approx(before.position.x + 2.0f));
    moved.position.x += 1.0f;
    editor.setSelectionTransform(moved);
    CHECK(editor.confirmation().empty());
    CHECK(editor.history().size() == 2);
    // A vob of the editor (170, not generated) needs no question at all.
    CHECK(editor.warningFor(world::VobId{170}).empty());

    // Saving: first the summary of what the next generator run writes anew; no keeps the file.
    const std::string original = fs::readText(copy.file).value();
    CHECK_FALSE(editor.save().ok());
    CHECK(editor.confirmation().find("1 generated vob(s) or waynet entries changed (68)") !=
          std::string::npos);
    editor.confirm(false);
    CHECK(fs::readText(copy.file).value() == original);
    CHECK_FALSE(editor.save().ok());
    editor.confirm(true);
    CHECK(fs::readText(copy.file).value() != original);
    CHECK_FALSE(editor.dirty());
}

TEST_CASE("Editor GPU: waynet mode - add, join, probe a path, rename, undo, delete, save and load again")
{
    WorldCopy copy;
    {
        Engine engine(editorConfig(copy.file));
        REQUIRE(engine.init().ok());
        editor::Editor editor(engine);
        engine.addTool(editor);
        editor.setWaynetMode(true);
        const world::WaynetPoint* outsidePoint = editor.waynetData().findPoint("WP_CAMP_OUTSIDE");
        REQUIRE(outsidePoint != nullptr);
        const Vec3 outside = outsidePoint->position; // a copy: edits replace the editor's waynet
        const usize points = editor.waynetData().points.size();

        // A new point next to the selected one: named after its stem, joined to it, known to the path
        // finding.
        editor.selectWaynet("WP_CAMP_OUTSIDE");
        const auto added = editor.addWaypoint(outside + Vec3(8.0f, 0.0f, 0.0f));
        REQUIRE(added.ok());
        CHECK(added.value() == "WP_CAMP_OUTSIDE_01");
        CHECK(editor.waynetSelection() == "WP_CAMP_OUTSIDE_01");
        CHECK(engine.waynet().find("WP_CAMP_OUTSIDE_01").has_value());
        const auto path = editor.probePath("WP_CAMP_CENTER", "WP_CAMP_OUTSIDE_01");
        REQUIRE(path.has_value());
        CHECK(path->back() == "WP_CAMP_OUTSIDE_01");
        CHECK(path->at(path->size() - 2) == "WP_CAMP_OUTSIDE");

        // Parted, it is unreachable (and the check finds it); joined again, reachable.
        CHECK_FALSE(editor.toggleEdge("WP_CAMP_OUTSIDE", "WP_CAMP_OUTSIDE_01").value());
        CHECK_FALSE(editor.probePath("WP_CAMP_CENTER", "WP_CAMP_OUTSIDE_01").has_value());
        const auto& issues = editor.checkWaynet();
        CHECK(std::ranges::any_of(
            issues, [](const editor::WaynetIssue& i)
            { return i.name == "WP_CAMP_OUTSIDE_01" && i.kind == editor::WaynetIssue::Kind::Unconnected; }));
        REQUIRE(editor.undo().value());
        CHECK(editor.probePath("WP_CAMP_CENTER", "WP_CAMP_OUTSIDE_01").has_value());

        // Rename and undo it; the edges follow both ways.
        REQUIRE(editor.renameWaynetSelection("WP_CAMP_BACH").ok());
        CHECK(editor.probePath("WP_CAMP_CENTER", "WP_CAMP_BACH").has_value());
        REQUIRE(editor.undo().value());
        CHECK(editor.waynetData().findPoint("WP_CAMP_BACH") == nullptr);
        CHECK(editor.probePath("WP_CAMP_CENTER", "WP_CAMP_OUTSIDE_01").has_value());
        REQUIRE(editor.redo().value());

        // A freepoint near it; deleting a point of the human's leaves no annotation line.
        editor.selectWaynet("WP_CAMP_BACH");
        REQUIRE(editor.addFreepoint("STAND", outside + Vec3(8.0f, 0.0f, 1.0f), 90.0f).ok());
        CHECK(editor.waynetData().findFreepoint("FP_STAND_CAMP_BACH_01") != nullptr);
        CHECK(engine.runFrame()); // waynet drawn, its window shown (selection, issues)
        editor.selectWaynet("WP_CAMP_FIRE");
        REQUIRE(editor.deleteWaynetSelection().ok());
        CHECK(editor.waynetAnnotation().empty());
        CHECK_FALSE(engine.waynet().find("WP_CAMP_FIRE").has_value());
        REQUIRE(editor.undo().value());
        CHECK(engine.waynet().find("WP_CAMP_FIRE").has_value());

        CHECK(editor.dirty());
        REQUIRE(editor.save().ok());
        CHECK(editor.waynetData().points.size() == points + 1);
    }
    // Loaded again: the waynet as saved.
    Engine engine(editorConfig(copy.file));
    REQUIRE(engine.init().ok());
    CHECK(engine.waynet().find("WP_CAMP_BACH").has_value());
    CHECK(engine.waynet().find("WP_CAMP_FIRE").has_value());
    REQUIRE(engine.worldFile().waynet.has_value());
    CHECK(engine.worldFile().waynet->findFreepoint("FP_STAND_CAMP_BACH_01") != nullptr);
}

TEST_CASE("Editor GPU: zones and triggers - a music zone at the camera switches the theme; undo; save; check")
{
    WorldCopy copy;
    {
        Engine engine(editorConfig(copy.file));
        REQUIRE(engine.init().ok());
        editor::Editor editor(engine);
        engine.addTool(editor);
        if (engine.music() == nullptr)
        {
            MESSAGE("no music (data/music.toml or sound missing) - zone part skipped");
            return;
        }
        // The camera in the camp: the camp's zone counts.
        engine.camera().transform.position = Vec3(8.0f, 3.0f, 4.0f);
        for (int i = 0; i < 3; ++i)
        {
            CHECK(engine.runFrame());
        }
        CHECK(engine.music()->status().theme == "LAGER");
        const std::vector<std::string> themes = editor.zoneValueChoices("music");
        CHECK(std::ranges::find(themes, "STADT") != themes.end());
        CHECK(std::ranges::find(themes, "common") == themes.end());

        // A smaller music zone around the camera: it counts at once (the smallest box wins).
        editor.setZoneMode(true);
        const usize zonesBefore = editor.zones().size();
        const auto added = editor.addZone("music", "STADT", Vec3(8.0f, 3.0f, 4.0f));
        REQUIRE(added.ok());
        CHECK(editor.zoneSelection() == added.value());
        CHECK(engine.worldFile().zones.size() == zonesBefore + 1);
        CHECK(engine.runFrame()); // zone mode drawn, its window shown
        CHECK(engine.music()->status().theme == "STADT");
        CHECK(editor.zones()[*editor.zoneAt("music", Vec3(8.0f, 3.0f, 4.0f))].value == "STADT");

        // Sized and moved away: the camp's theme again; undone: the town's.
        world::ZoneBox box = *editor.zones()[added.value()].box;
        box.center.x += 200.0f;
        REQUIRE(editor.setZoneBox(box).ok());
        CHECK(engine.runFrame());
        CHECK(engine.music()->status().theme == "LAGER");
        REQUIRE(editor.undo().value());
        CHECK(engine.runFrame());
        CHECK(engine.music()->status().theme == "STADT");
        CHECK_FALSE(editor.addZone("fog", "dense", Vec3(0.0f)).ok()); // other types are not edited

        // Triggers: the camp's level change to the cave is fine; a broken function name is found.
        editor.setZoneMode(false);
        const auto& issues = editor.checkTriggers();
        CHECK(std::ranges::none_of(issues, [](const editor::TriggerIssue& i) { return i.id.value == 173; }));
        REQUIRE(editor.save().ok());
    }
    Engine engine(editorConfig(copy.file));
    REQUIRE(engine.init().ok());
    CHECK(std::ranges::any_of(engine.worldFile().zones,
                              [](const world::Zone& z) { return z.type == "music" && z.value == "STADT"; }));
}

TEST_CASE("Editor GPU: lights and mobs - a new light is in the frame's choice at once; the time slider")
{
    WorldCopy copy;
    Engine engine(editorConfig(copy.file));
    REQUIRE(engine.init().ok());
    editor::Editor editor(engine);
    engine.addTool(editor);
    const auto shot = [&](const char* name)
    {
        const fs::Path file = copy.dir / name;
        engine.requestScreenshot(file);
        CHECK(engine.runFrame());
        auto bytes = fs::readText(file);
        REQUIRE(bytes.ok());
        return bytes.value();
    };

    // The time slider: night, then noon - the sky follows (display only).
    editor.setPreviewHour(0.5f);
    CHECK(engine.runFrame());
    const f32 nightSun = engine.environment().sunIntensity;
    editor.setPreviewHour(12.0f);
    CHECK(engine.runFrame());
    CHECK(engine.gameTime().hourOfDay() == doctest::Approx(12.0f));
    CHECK(engine.environment().sunIntensity > nightSun);

    // By night a fire 4 m ahead: in this frame's lights at once, and the picture changes.
    editor.setPreviewHour(0.5f);
    const std::string dark = shot("before.png");
    const usize worldLights = engine.worldLightCount();
    const auto placed = editor.placeLight("fire");
    REQUIRE(placed.ok());
    CHECK(engine.worldLightCount() == worldLights + 1);
    const Vec3 at =
        editor::worldTransformOf(engine.scene(), engine.scene().findById(placed.value())).position;
    const std::string lit = shot("after.png");
    const auto chosen = [&]
    {
        return std::ranges::any_of(engine.frameLights().lights(), [&](const render::PointLight& l)
                                   { return glm::length(l.position - at) < 0.01f; });
    };
    CHECK(chosen());
    CHECK(lit != dark);
    CHECK_FALSE(editor.placeLight("laser").ok());

    // Undone: gone from the choice.
    REQUIRE(editor.undo().value());
    CHECK(engine.runFrame());
    CHECK_FALSE(chosen());
    CHECK(engine.worldLightCount() == worldLights);

    // Mobs: the camp's definitions come from the scripts; the check runs over the camp's mob vobs.
    const editor::MobLists lists = editor.mobLists();
    CHECK_FALSE(lists.definitions.empty());
    CHECK(std::ranges::find(lists.types, "chest") != lists.types.end());
    CHECK(std::ranges::find(lists.guilds, "guard") != lists.guilds.end());
    // The camp's mobs are all known (the stool sits like a bench); a mob of an unknown type is found.
    CHECK(editor.checkMobs().empty());
    auto crate = engine.scene().spawnVob({"CRATE_TEST", Transform{}, {}, {}});
    REQUIRE(crate.ok());
    engine.scene().set<world::MobRef>(crate.value(), {"crate_of_nowhere"});
    const auto& issues = editor.checkMobs();
    REQUIRE(issues.size() == 1);
    CHECK(issues[0].name == "CRATE_TEST");
    CHECK(issues[0].text.find("crate_of_nowhere") != std::string::npos);
}

TEST_CASE(
    "Editor GPU: routines - ghosts, the editor's own routine file, play-in-editor leaves the world as it was")
{
    WorldCopy copy;
    Engine engine(editorConfig(copy.file));
    REQUIRE(engine.init().ok());
    editor::Editor editor(engine);
    engine.addTool(editor);
    REQUIRE(engine.runConsoleLine("camp_people()").ok()); // the camp's NPCs at their routine places
    CHECK(engine.runFrame());

    // Ghosts: at 8 o'clock the farmer woman sweeps at the camp's centre (routines/camp.lua), from the
    // scripts.
    const auto ghosts = editor.ghosts(8 * 60);
    const auto woman = std::ranges::find(ghosts, "npc_farmer_woman", &editor::Editor::Ghost::npc);
    REQUIRE(woman != ghosts.end());
    CHECK(woman->state == "zs_sweep");
    CHECK(woman->at == "wp_camp_center");
    CHECK(woman->place.has_value());
    for (const editor::RoutineIssue& issue : editor.checkRoutines())
    {
        MESSAGE(issue.routine << ": " << issue.text);
    }
    CHECK(editor.checkRoutines().empty()); // the camp's day plans are whole

    // Decision 8: the selected routine's NPC stands there as its real figure (at rest).
    editor.setRoutineHour(8.0f);
    editor.selectRoutine("rtn_farmer_woman");
    engine.camera().transform.position =
        *woman->place + Vec3(4.0f, 1.6f, 0.0f); // the fly camera keeps its yaw (90: along -X)
    CHECK(engine.runFrame());
    CHECK(engine.ghostFigureShown());
    editor.selectRoutine("");
    CHECK(engine.runFrame());
    CHECK_FALSE(engine.ghostFigureShown());

    // A routine of the editor's file: made, filled, undone and redone, written - the scripts load it.
    CHECK_FALSE(editor.newRoutine("rtn_farmer_woman").ok()); // hand-written: taken
    REQUIRE(editor.newRoutine("rtn_editor_guard").ok());
    REQUIRE(editor.addRoutineEntry("rtn_editor_guard", {"20:00", "08:00", "zs_sleep", "wp_camp_west"}).ok());
    REQUIRE(
        editor.addRoutineEntry("rtn_editor_guard", {"08:00", "20:00", "zs_stand", "wp_camp_center"}).ok());
    CHECK_FALSE(
        editor.addRoutineEntry("rtn_farmer_woman", {"08:00", "09:00", "zs_stand", "wp_camp_center"}).ok());
    CHECK_FALSE(editor.addRoutineEntry("rtn_editor_guard", {"08:00", "09:00", "zs_stand", "wp\"x"}).ok());
    REQUIRE(editor.undo().value());
    CHECK(editor.editorRoutines()[0].entries.size() == 1);
    REQUIRE(editor.redo().value());
    CHECK(editor.routinesDirty());
    REQUIRE(editor.writeRoutines().ok());
    CHECK_FALSE(editor.routinesDirty());
    CHECK(std::filesystem::exists(copy.dir / "scripts" / "routines" /
                                  "camp_copy_editor.lua")); // site: the file name
    const auto loaded = engine.routines();
    const auto guard = std::ranges::find(loaded, "rtn_editor_guard", &RoutineInfo::name);
    REQUIRE(guard != loaded.end());
    CHECK(guard->file == "routines/camp_copy_editor.lua");
    REQUIRE(guard->entries.size() == 2);
    CHECK(guard->entries[0].from == "08:00"); // sorted by start
    {
        editor::Editor again(engine); // a new session finds its routines in the file
        REQUIRE(again.editorRoutines().size() == 1);
        CHECK(again.editorRoutines()[0] == *guard);
    }

    // Play-in-editor at the camera: the hero there, the simulation runs; stopped, the world is as before.
    const auto text = [&]
    {
        world::WorldFile file = world::captureWorld(engine.scene(), "w");
        return world::writeWorldFile(file);
    };
    const std::string before = text();
    const f64 minuteBefore = engine.gameTime().minuteOfDay();
    const usize npcsBefore = engine.npcRoutines().size();
    const Transform camera = engine.camera().transform;
    REQUIRE(editor.startPlay(false).ok());
    CHECK(editor.playing());
    CHECK_FALSE(editor.startPlay(false).ok());
    for (int i = 0; i < 30; ++i)
    {
        CHECK(engine.runFrame()); // the routine window only, with its stop button
    }
    CHECK(engine.simulationTicks() > 0);
    REQUIRE(engine.runConsoleLine("insert('it_apple', 3)").ok()); // something changes while playing
    REQUIRE(editor.stopPlay().ok());
    CHECK_FALSE(editor.playing());
    CHECK(text() == before);
    CHECK(engine.npcRoutines().size() == npcsBefore);
    CHECK(engine.gameTime().minuteOfDay() == doctest::Approx(minuteBefore).epsilon(0.001));
    CHECK(glm::length(engine.camera().transform.position - camera.position) < 1e-3f);
    CHECK(engine.runFrame());

    // Shift+F5: at the start point.
    REQUIRE(editor.startPlay(true).ok());
    CHECK(engine.runFrame());
    REQUIRE(editor.stopPlay().ok());
    CHECK(text() == before);
}
