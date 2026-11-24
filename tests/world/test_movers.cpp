// Moving parts (docs/modules/dungeon.md D2): components.mover in .g7world, the motion of each kind, the
// course over time, and the geometry for crushing and for blocked waynet edges.

#include <g7/world/Movers.hpp>
#include <g7/world/Scene.hpp>
#include <g7/world/WorldFile.hpp>
#include <g7/world/WorldScene.hpp>

#include <doctest/doctest.h>

#include <ostream>
#include <string>

using namespace g7;
using namespace g7::world;

namespace
{
WorldFile parse(std::string_view text)
{
    auto world = parseWorldFile(text, "movers.g7world");
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

bool near(const Vec3& a, const Vec3& b)
{
    return glm::length(a - b) < 1e-4f;
}

const AABB kBlock{Vec3(-0.5f, 0.0f, -0.5f), Vec3(0.5f, 1.0f, 0.5f)}; // testworld/block.gltf
} // namespace

TEST_CASE("Mover format: components.mover at mesh and mob vobs, written byte-equal")
{
    const WorldFile file = parse(R"({"version": 1, "name": "m", "vobs": [
  {"id": 1, "type": "mesh", "name": "GATE_E01_1_2", "mesh": "a.glb", "components": {"mover": {"kind": "gate"}}},
  {"id": 2, "type": "mob", "name": "WALL_SCHLOSS_FASS", "mesh": "b.glb",
   "components": {"mob": {"definition": "x"}, "mover": {"kind": "secret_wall", "open": true, "seconds": 4,
     "offset": [1.5, 0, 0], "axis": [0, 1, 0], "angle": 30, "sound_move": "stone_move", "sound_stop": "stone_stop",
     "crush": "damage"}}},
  {"id": 3, "type": "mesh", "name": "TRAP_E01_3_3", "mesh": "c.glb", "components": {"mover": {"kind": "trapdoor"}}},
  {"id": 4, "type": "mesh", "name": "PLAIN", "mesh": "d.glb"}
]})");
    REQUIRE(file.vobs[0].mover.has_value());
    const Mover& gate = *file.vobs[0].mover;
    CHECK(gate.kind == Mover::Kind::Gate);
    CHECK_FALSE(gate.open);
    CHECK(gate.seconds == 2.0f); // the kind's default
    CHECK_FALSE(gate.offset.has_value());
    CHECK(gate.crush == Mover::Crush::Stop);
    const Mover& wall = *file.vobs[1].mover;
    CHECK(wall.kind == Mover::Kind::SecretWall);
    CHECK(wall.open);
    CHECK(wall.seconds == 4.0f);
    CHECK(near(*wall.offset, Vec3(1.5f, 0.0f, 0.0f)));
    CHECK(near(*wall.axis, Vec3(0.0f, 1.0f, 0.0f)));
    CHECK(*wall.angle == 30.0f);
    CHECK(wall.soundMove == "stone_move");
    CHECK(wall.soundStop == "stone_stop");
    CHECK(wall.crush == Mover::Crush::Damage);
    CHECK(file.vobs[2].mover->seconds == 0.6f);
    CHECK_FALSE(file.vobs[3].mover.has_value());

    Scene scene;
    REQUIRE(spawnWorld(scene, file).ok());
    CHECK(scene.get<Mover>(scene.findById(VobId{2}))->kind == Mover::Kind::SecretWall);
    CHECK(scene.get<Mover>(scene.findById(VobId{4})) == nullptr);
    const std::string written = writeWorldFile(captureWorld(scene, "m"));
    CHECK(writeWorldFile(parse(written)) == written);
    CHECK(
        written.find(R"("components":{"mover":{"kind":"gate","open":false,"seconds":2.0,"crush":"stop"}})") !=
        std::string::npos);
    CHECK(written.find(
              R"("mover":{"kind":"secret_wall","open":true,"seconds":4.0,"offset":[1.5,0.0,0.0],)"
              R"("axis":[0.0,1.0,0.0],"angle":30.0,"sound_move":"stone_move","sound_stop":"stone_stop",)"
              R"("crush":"damage"})") != std::string::npos);
    CHECK(written.find(R"("name":"PLAIN","pos":[0.0,0.0,0.0],"rot":[0.0,0.0,0.0,1.0],"mesh":"d.glb"})") !=
          std::string::npos);
}

TEST_CASE("Mover format: broken movers are errors naming the entry")
{
    CHECK(errorOf(R"({"id": 1, "type": "mesh", "mesh": "a.glb", "components": {"mover": {}}})") ==
          "w.g7world: vobs[0].components.mover: needs 'kind' ('gate', 'secret_wall' or 'trapdoor')");
    CHECK(
        errorOf(R"({"id": 1, "type": "mesh", "mesh": "a.glb", "components": {"mover": {"kind": "lift"}}})") ==
        "w.g7world: vobs[0].components.mover: 'kind' must be one of 'gate', 'secret_wall', 'trapdoor'");
    CHECK(errorOf(R"({"id": 1, "type": "mesh", "mesh": "a.glb",
                      "components": {"mover": {"kind": "gate", "crush": "squash"}}})") ==
          "w.g7world: vobs[0].components.mover: 'crush' must be one of 'stop', 'damage'");
    CHECK(errorOf(R"({"id": 1, "type": "mesh", "mesh": "a.glb",
                      "components": {"mover": {"kind": "gate", "seconds": 0}}})") ==
          "w.g7world: vobs[0].components.mover.seconds: must be more than 0");
    CHECK(errorOf(R"({"id": 1, "type": "mesh", "mesh": "a.glb",
                      "components": {"mover": {"kind": "trapdoor", "axis": [0, 0, 0]}}})") ==
          "w.g7world: vobs[0].components.mover.axis: must not be zero");
    CHECK(errorOf(R"({"id": 1, "type": "mesh", "mesh": "a.glb",
                      "components": {"mover": {"kind": "gate", "open": "yes"}}})") ==
          "w.g7world: vobs[0].components.mover.open: must be true or false");
    CHECK(errorOf(R"({"id": 1, "type": "trigger", "components": {"mover": {"kind": "gate"}}})") ==
          "w.g7world: vobs[0]: 'components.mover' belongs to a mesh or mob vob");
}

TEST_CASE("Mover motion: each kind's default from the model's bounds, overrides from the file")
{
    Mover gate;
    gate.kind = Mover::Kind::Gate;
    const MoverMotion up = moverMotion(gate, kBlock, Vec3(3.0f, 3.0f, 0.2f));
    CHECK(near(up.offset, Vec3(0.0f, 3.0f, 0.0f)));
    CHECK(up.angle == 0.0f);

    Mover wall;
    wall.kind = Mover::Kind::SecretWall;
    CHECK(near(moverMotion(wall, kBlock, Vec3(3.0f, 3.0f, 0.5f)).offset, Vec3(0.0f, 0.0f, -0.5f)));
    wall.offset = Vec3(3.0f, 0.0f, 0.0f);
    CHECK(near(moverMotion(wall, kBlock, Vec3(3.0f, 3.0f, 0.5f)).offset, Vec3(3.0f, 0.0f, 0.0f)));

    Mover trap;
    trap.kind = Mover::Kind::Trapdoor;
    const MoverMotion down = moverMotion(trap, kBlock, Vec3(2.0f, 0.1f, 2.0f));
    CHECK(near(down.offset, Vec3(0.0f)));
    CHECK(near(down.axis, Vec3(1.0f, 0.0f, 0.0f)));
    CHECK(down.angle == doctest::Approx(glm::radians(90.0f)));
    trap.axis = Vec3(0.0f, 0.0f, 2.0f);
    trap.angle = -45.0f;
    const MoverMotion other = moverMotion(trap, kBlock, Vec3(1.0f));
    CHECK(near(other.axis, Vec3(0.0f, 0.0f, 1.0f)));
    CHECK(other.angle == doctest::Approx(glm::radians(-45.0f)));
}

TEST_CASE("Mover pose: slides in the vob's turned frame, the trapdoor's leaf goes down")
{
    Transform closed;
    closed.position = Vec3(10.0f, 0.0f, 5.0f);
    closed.rotation = glm::angleAxis(glm::radians(90.0f), Vec3(0.0f, 1.0f, 0.0f));
    closed.scale = Vec3(3.0f, 3.0f, 0.5f);
    MoverMotion back;
    back.offset = Vec3(0.0f, 0.0f, -0.5f);
    CHECK(moverPose(closed, back, 0.0f).position == closed.position);
    // Turned 90 degrees about Y: the vob's -Z is the world's -X.
    CHECK(near(moverPose(closed, back, 1.0f).position, Vec3(9.5f, 0.0f, 5.0f)));
    CHECK(near(moverPose(closed, back, 0.5f).position, Vec3(9.75f, 0.0f, 5.0f))); // eased: half way at half
    CHECK(moverPose(closed, back, 1.0f).scale == closed.scale);

    MoverMotion trap;
    trap.angle = glm::radians(90.0f);
    const Transform open = moverPose(Transform{}, trap, 1.0f);
    CHECK(near(open.rotation * Vec3(0.0f, 0.0f, 1.0f), Vec3(0.0f, -1.0f, 0.0f)));
    CHECK(near(open.position, Vec3(0.0f)));
}

TEST_CASE("Mover course: moves at the way's speed, arrives once, turns back")
{
    MoverCourse course;
    CHECK_FALSE(course.moving());
    CHECK_FALSE(course.advance(1.0f, 2.0f));
    course.target = 1.0f;
    CHECK(course.moving());
    CHECK_FALSE(course.closing());
    CHECK_FALSE(course.advance(0.5f, 2.0f));
    CHECK(course.position == doctest::Approx(0.25f));
    course.target = 0.0f; // back
    CHECK(course.closing());
    CHECK(course.advance(1.0f, 2.0f));
    CHECK(course.position == 0.0f);
    CHECK_FALSE(course.moving());
}

TEST_CASE("Mover geometry: oriented boxes, capsules in the way, waynet edges through it")
{
    // A gate 3 m wide, 3 m high, 0.2 m deep at z = 5.
    const Mat4 world =
        Transform{Vec3(0.0f, 0.0f, 5.0f), Quat(1.0f, 0.0f, 0.0f, 0.0f), Vec3(3.0f, 3.0f, 0.2f)}.toMatrix();
    const OrientedBox box = orientedBox(kBlock, world);
    CHECK(near(box.center, Vec3(0.0f, 1.5f, 5.0f)));
    CHECK(near(box.half, Vec3(1.5f, 1.5f, 0.1f)));
    CHECK(distanceTo(box, Vec3(0.0f, 1.0f, 5.0f)) == 0.0f);
    CHECK(distanceTo(box, Vec3(0.0f, 1.0f, 6.1f)) == doctest::Approx(1.0f));

    // Someone standing in the gateway, and someone 0.3 m in front of it (touching, not in it).
    CHECK(capsuleInBox(box, Vec3(0.0f, 0.0f, 5.0f), 0.3f, 1.8f));
    CHECK_FALSE(capsuleInBox(box, Vec3(0.0f, 0.0f, 5.4f), 0.3f, 1.8f));
    // The gate high up (its bottom at 2.5 m) misses a man of 1.8 m, low (1.5 m) it hits his head.
    const auto at = [](f32 bottom)
    {
        return orientedBox(
            kBlock, Transform{Vec3(0.0f, bottom, 5.0f), Quat(1.0f, 0.0f, 0.0f, 0.0f), Vec3(3.0f, 3.0f, 0.2f)}
                        .toMatrix());
    };
    CHECK_FALSE(capsuleInBox(at(2.5f), Vec3(0.0f, 0.0f, 5.0f), 0.3f, 1.8f));
    CHECK(capsuleInBox(at(1.5f), Vec3(0.0f, 0.0f, 5.0f), 0.3f, 1.8f));

    CHECK(segmentCrossesBox(box, Vec3(0.0f, 0.3f, 0.0f), Vec3(0.0f, 0.3f, 10.0f)));
    CHECK_FALSE(segmentCrossesBox(box, Vec3(0.0f, 0.3f, 0.0f), Vec3(0.0f, 0.3f, 4.0f)));  // ends before
    CHECK_FALSE(segmentCrossesBox(box, Vec3(2.0f, 0.3f, 0.0f), Vec3(2.0f, 0.3f, 10.0f))); // beside it
    CHECK_FALSE(segmentCrossesBox(box, Vec3(0.0f, 3.5f, 0.0f), Vec3(0.0f, 3.5f, 10.0f))); // above it

    CHECK(moverBlocks(Mover::Kind::Gate, MoverCourse{0.0f, 0.0f}));
    CHECK(moverBlocks(Mover::Kind::Gate, MoverCourse{0.9f, 1.0f}));
    CHECK_FALSE(moverBlocks(Mover::Kind::Gate, MoverCourse{1.0f, 1.0f}));
    CHECK_FALSE(moverBlocks(Mover::Kind::Trapdoor, MoverCourse{0.0f, 0.0f}));
    CHECK(moverBlocks(Mover::Kind::Trapdoor, MoverCourse{0.0f, 1.0f}));
    CHECK(moverBlocks(Mover::Kind::SecretWall, MoverCourse{1.0f, 0.0f}));
}
