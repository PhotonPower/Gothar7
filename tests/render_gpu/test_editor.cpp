// Editor mode end to end (label "gpu", M4 DoD: place vobs in the editor and save): on a copy of the
// camp world in a temporary folder, never on the repository's file.

#include "GlFixture.hpp"

#include <g7/editor/Editor.hpp>
#include <g7/editor/Operations.hpp>
#include <g7/runtime/Engine.hpp>
#include <g7/world/Scene.hpp>
#include <g7/world/WorldFile.hpp>

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
    config.shaderDirectory = fs::fromUtf8(G7_SHADER_DIR);
    config.fixedFrameSeconds = 1.0 / 60.0;
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
