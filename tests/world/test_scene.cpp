#include <g7/world/Scene.hpp>

#include <doctest/doctest.h>
#include <glm/gtc/matrix_transform.hpp>

#include <ostream>
#include <set>
#include <string>

using namespace g7;
using namespace g7::world;

namespace
{
entt::entity spawn(Scene& scene, VobDesc desc)
{
    auto vob = scene.spawnVob(desc);
    REQUIRE_MESSAGE(vob.ok(), (vob.ok() ? "" : vob.error().message));
    return vob.value();
}

Transform at(const Vec3& position, f32 yawDegrees = 0.0f, f32 scale = 1.0f)
{
    Transform t;
    t.position = position;
    t.rotation = quatFromEuler(0.0f, toRadians(yawDegrees), 0.0f);
    t.scale = Vec3(scale);
    return t;
}

Vec3 origin(const Mat4& m)
{
    return Vec3(m[3]);
}

/// doctest and entt both offer an operator== for entity == entt::null; compare outside CHECK.
bool isNull(entt::entity e)
{
    return e == entt::null;
}

/// A component of the game layer, unknown to world.
struct Health
{
    i32 value = 100;
};
} // namespace

TEST_CASE("Scene: ids are unique, rising and never reused")
{
    Scene scene;
    const auto a = spawn(scene, {StringId("A")});
    const auto b = spawn(scene, {StringId("B")});
    CHECK(scene.idOf(a) == VobId{1});
    CHECK(scene.idOf(b) == VobId{2});
    scene.destroyVob(b);
    CHECK_FALSE(scene.valid(b));
    CHECK(isNull(scene.findById(VobId{2})));
    const auto c = spawn(scene, {StringId("C")});
    CHECK(scene.idOf(c) == VobId{3}); // 2 stays gone
    CHECK(scene.nextVobId() == 4);
    CHECK(scene.vobCount() == 2);
}

TEST_CASE("Scene: runtime vobs come from their own range and counter")
{
    Scene scene;
    const auto item = spawn(scene, {StringId("ITEM"), {}, {}, {}, true});
    CHECK(scene.idOf(item).runtime());
    CHECK(scene.idOf(item) == VobId{kRuntimeVobIdBase});
    const auto placed = spawn(scene, {StringId("HUT")});
    CHECK(scene.idOf(placed) == VobId{1}); // the world counter is untouched
    CHECK(scene.nextRuntimeVobId() == kRuntimeVobIdBase + 1);

    // M15: the save game restores the runtime counter; it only rises.
    CHECK(scene.setNextRuntimeVobId(kRuntimeVobIdBase + 50).ok());
    CHECK(scene.idOf(spawn(scene, {StringId("ITEM2"), {}, {}, {}, true})) == VobId{kRuntimeVobIdBase + 50});
    CHECK_FALSE(scene.setNextRuntimeVobId(kRuntimeVobIdBase + 10).ok());
}

TEST_CASE("Scene: loading with fixed ids")
{
    Scene scene;
    const auto hut = spawn(scene, {StringId("HUT"), {}, {}, VobId{101}});
    CHECK(scene.idOf(hut) == VobId{101});
    CHECK(scene.nextVobId() == 102); // at least max(id) + 1
    spawn(scene, {StringId("FIRE"), {}, {}, VobId{7}});
    CHECK(scene.nextVobId() == 102);

    // Data errors are Results, not asserts.
    CHECK_FALSE(scene.spawnVob({StringId("DUP"), {}, {}, VobId{101}}).ok());
    CHECK_FALSE(scene.spawnVob({StringId("BAD"), {}, {}, VobId{kRuntimeVobIdBase + 3}}).ok());
    CHECK_FALSE(scene.spawnVob({StringId("RT"), {}, {}, VobId{5}, true}).ok()); // runtime with a fixed id
    CHECK_FALSE(scene.spawnVob({StringId("ORPHAN"), {}, VobId{999}}).ok());     // unknown parent
    CHECK(scene.vobCount() == 2);

    // The file's counter may be higher; it never goes down.
    CHECK(scene.setNextVobId(500).ok());
    CHECK(scene.nextVobId() == 500);
    CHECK_FALSE(scene.setNextVobId(200).ok());
    CHECK(scene.nextVobId() == 500);
    CHECK_FALSE(scene.setNextVobId(kRuntimeVobIdBase + 1).ok());
}

TEST_CASE("Scene: find by id and name, components of other modules")
{
    Scene scene;
    const auto hut = spawn(scene, {StringId("HUT_01")});
    const auto fire = spawn(scene, {StringId("CAMPFIRE")});
    CHECK(scene.findByName(StringId("campfire")) == fire); // StringId is case-insensitive
    CHECK(isNull(scene.findByName(StringId("NOPE"))));
    CHECK(scene.findById(scene.idOf(hut)) == hut);
    CHECK(scene.idOf(entt::null) == VobId{});

    scene.set<Health>(hut, {40});
    CHECK(scene.has<Health>(hut));
    CHECK_FALSE(scene.has<Health>(fire));
    scene.get<Health>(hut)->value -= 10;
    CHECK(scene.get<Health>(hut)->value == 30);
    CHECK(scene.get<Health>(fire) == nullptr);

    int count = 0;
    scene.each<Vob, Health>(
        [&](entt::entity e, Vob& vob, Health& health)
        {
            CHECK(e == hut);
            CHECK(vob.name == StringId("HUT_01"));
            CHECK(health.value == 30);
            ++count;
        });
    CHECK(count == 1);
    scene.remove<Health>(hut);
    CHECK_FALSE(scene.has<Health>(hut));
}

TEST_CASE("Scene: world transforms follow the hierarchy")
{
    Scene scene;
    const auto camp = spawn(scene, {StringId("CAMP"), at(Vec3(10, 0, 0), 90.0f)});
    const auto hut = spawn(scene, {StringId("HUT"), at(Vec3(0, 0, -4)), scene.idOf(camp)});
    const auto door = spawn(scene, {StringId("DOOR"), at(Vec3(1, 0, 0), 0.0f, 2.0f), scene.idOf(hut)});
    scene.updateTransforms();
    // Camp turned 90° left: local -Z becomes world -X.
    CHECK(nearlyEqual(origin(scene.get<WorldTransform>(hut)->matrix), Vec3(6, 0, 0), 1e-4f));
    // Three levels: door = camp * hut * door; its local +X becomes world -Z.
    CHECK(nearlyEqual(origin(scene.get<WorldTransform>(door)->matrix), Vec3(6, 0, -1), 1e-4f));
    CHECK(nearlyEqual(origin(scene.worldMatrix(door)), Vec3(6, 0, -1), 1e-4f));
    CHECK(scene.parent(door) == hut);
    CHECK(scene.children(camp) == std::vector<entt::entity>{hut});

    // Moving the camp moves everything below it.
    scene.setTransform(camp, at(Vec3(20, 0, 0), 90.0f));
    scene.updateTransforms();
    CHECK(nearlyEqual(origin(scene.get<WorldTransform>(door)->matrix), Vec3(16, 0, -1), 1e-4f));
}

TEST_CASE("Scene: only changed subtrees are recomputed")
{
    Scene scene;
    const auto a = spawn(scene, {StringId("A"), at(Vec3(1, 0, 0))});
    const auto b = spawn(scene, {StringId("B"), at(Vec3(0, 0, 5))});
    scene.updateTransforms();
    // Tamper with B's cached matrix: a clean subtree keeps it, proving it was not recomputed.
    scene.get<WorldTransform>(b)->matrix = Mat4(2.0f);
    scene.setTransform(a, at(Vec3(3, 0, 0)));
    scene.updateTransforms();
    CHECK(nearlyEqual(origin(scene.get<WorldTransform>(a)->matrix), Vec3(3, 0, 0)));
    CHECK(scene.get<WorldTransform>(b)->matrix == Mat4(2.0f));
    // A change of B (even through its parent) brings it back.
    scene.setTransform(b, at(Vec3(0, 0, 5)));
    scene.updateTransforms();
    CHECK(nearlyEqual(origin(scene.get<WorldTransform>(b)->matrix), Vec3(0, 0, 5)));
}

TEST_CASE("Scene: re-parenting keeps the world placement and rejects cycles")
{
    Scene scene;
    const auto camp = spawn(scene, {StringId("CAMP"), at(Vec3(10, 0, 0), 90.0f, 2.0f)});
    const auto torch = spawn(scene, {StringId("TORCH"), at(Vec3(3, 1, 2))});
    const auto flame = spawn(scene, {StringId("FLAME"), at(Vec3(0, 0.5f, 0)), scene.idOf(torch)});
    const Mat4 before = scene.worldMatrix(flame);

    REQUIRE(scene.setParent(torch, camp).ok());
    scene.updateTransforms();
    CHECK(scene.parent(torch) == camp);
    CHECK(nearlyEqual(origin(scene.get<WorldTransform>(flame)->matrix), origin(before), 1e-4f));
    CHECK(nearlyEqual(origin(scene.worldMatrix(torch)), Vec3(3, 1, 2), 1e-4f));

    CHECK_FALSE(scene.setParent(camp, flame).ok()); // flame lies below camp
    CHECK_FALSE(scene.setParent(camp, camp).ok());
    CHECK_FALSE(scene.setParent(entt::null, camp).ok());

    REQUIRE(scene.setParent(torch, entt::null).ok()); // back to the root
    CHECK(isNull(scene.parent(torch)));
    CHECK(scene.children(camp).empty());
    CHECK(nearlyEqual(origin(scene.worldMatrix(torch)), Vec3(3, 1, 2), 1e-4f));
}

TEST_CASE("Scene: destroying a vob destroys its descendants")
{
    Scene scene;
    const auto camp = spawn(scene, {StringId("CAMP")});
    const auto hut = spawn(scene, {StringId("HUT"), {}, scene.idOf(camp)});
    const auto door = spawn(scene, {StringId("DOOR"), {}, scene.idOf(hut)});
    const auto fire = spawn(scene, {StringId("FIRE"), {}, scene.idOf(camp)});
    const auto other = spawn(scene, {StringId("OTHER")});
    scene.destroyVob(hut);
    CHECK_FALSE(scene.valid(hut));
    CHECK_FALSE(scene.valid(door));
    CHECK(scene.valid(fire));
    CHECK(scene.children(camp) == std::vector<entt::entity>{fire});
    scene.destroyVob(camp);
    CHECK_FALSE(scene.valid(fire));
    CHECK(scene.valid(other));
    CHECK(scene.vobCount() == 1);
    scene.destroyVob(camp); // already gone: no effect
}

TEST_CASE("Scene: ten thousand vobs")
{
    Scene scene;
    const auto root = spawn(scene, {StringId("ROOT")});
    std::set<u64> ids;
    for (int i = 0; i < 10000; ++i)
    {
        const auto e =
            spawn(scene, {StringId("V"), at(Vec3(f32(i), 0, 0)), i % 2 == 0 ? scene.idOf(root) : VobId{}});
        ids.insert(scene.idOf(e).value);
    }
    CHECK(ids.size() == 10000);
    scene.updateTransforms();
    int moved = 0;
    scene.each<WorldTransform>([&](entt::entity, WorldTransform&) { ++moved; });
    CHECK(moved == 10001);
    scene.setTransform(root, at(Vec3(0, 10, 0)));
    scene.updateTransforms();
    CHECK(origin(scene.get<WorldTransform>(scene.findById(VobId{2}))->matrix).y == doctest::Approx(10.0f));
    CHECK(origin(scene.get<WorldTransform>(scene.findById(VobId{3}))->matrix).y == doctest::Approx(0.0f));
}
