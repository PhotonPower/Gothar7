// The game state as a JSON document (M15 part B): every field survives toJson -> fromJson, the text is
// stable, broken documents and newer versions are refused; the world delta finds removed, changed and new
// vobs and applies only where the vob still fits.

#include <g7/save/GameState.hpp>
#include <g7/save/WorldDelta.hpp>

#include <doctest/doctest.h>

#include <ostream> // doctest needs it to print std::string operands
#include <string>

using namespace g7;

namespace
{
world::WorldFileVob vob(u64 id, std::string name, Vec3 at)
{
    world::WorldFileVob v;
    v.id = world::VobId{id};
    v.type = world::VobType::Mesh;
    v.name = std::move(name);
    v.mesh = "m.glb";
    v.transform.position = at;
    return v;
}

save::GameState sample()
{
    save::GameState s;
    s.clock = 8.0 * 60.0 + 12.25;
    s.secondsPerMinute = 4.0;
    s.scriptClock = 123.5;
    s.nextTimerId = 7;
    s.timers.push_back({3, 130.0, 0.0, "camp_check", {script::Value("npc_x"), script::Value(i64{2})}});
    s.story = script::makeTable(
        {script::Value(i64{1}), script::Value(2.0)},
        {{"chapter", script::Value(i64{2})},
         {"name", script::Value("Ring")},
         {"done", script::Value(true)},
         {"quests",
          script::makeTable({}, {{"q", script::makeTable({}, {{"status", script::Value("running")}})}})}});
    gameplay::CharacterState hero;
    hero.instance = "pc_hero";
    hero.name = "Held";
    hero.attributes = {{"hp", 30}, {"hp_max", 40}};
    hero.talents = {{"melee_1h", 1}};
    hero.level = 2;
    hero.experience = 510;
    hero.learnPoints = 5;
    hero.items = {{"it_apple", 3}, {"it_sword_old", 1}};
    hero.equipped[static_cast<usize>(gameplay::EquipSlot::Melee)] = "it_sword_old";
    s.hero = save::HeroState{hero, Vec3(1.5f, 0.25f, -3.125f), 1.25f, "it_torch"};
    s.currentWorld = "testworld/camp.g7world";
    save::WorldState w;
    w.path = s.currentWorld;
    w.delta.removed.push_back({world::VobId{4}, "APPLE"});
    w.delta.changed.push_back(vob(9, "DOOR", Vec3(1.0f, 0.0f, 2.0f)));
    world::WorldFileVob dropped;
    dropped.id = world::VobId{world::kRuntimeVobIdBase + 2};
    dropped.type = world::VobType::Item;
    dropped.name = "it_apple";
    dropped.item = {"it_apple", 2, ""};
    w.delta.runtime.push_back(dropped);
    w.delta.nextVobId = 100;
    w.delta.nextRuntimeVobId = world::kRuntimeVobIdBase + 3;
    w.spentTriggers = {12, 15};
    w.mobs.push_back({20, "mob_camp_chest", false, true, {{"it_gulden", 10}}});
    save::NpcState npc;
    npc.name = "mon_wolf#2";
    npc.instance = "mon_wolf";
    npc.position = Vec3(-3.0f, 0.0f, 4.5f);
    npc.yaw = 0.5f;
    npc.routine = "rtn_mon_wolf";
    npc.life = save::Life::Dead;
    npc.character = hero;
    w.npcs.push_back(npc);
    s.worlds.push_back(std::move(w));
    return s;
}
} // namespace

TEST_CASE("Game state: everything survives JSON, the text is stable")
{
    const save::GameState s = sample();
    const std::string text = save::toJson(s);
    auto back = save::fromJson(text);
    REQUIRE_MESSAGE(back.ok(), (back.ok() ? "" : back.error().message));
    const save::GameState& b = back.value();
    CHECK(b.clock == s.clock);
    CHECK(b.scriptClock == s.scriptClock);
    CHECK(b.nextTimerId == 7);
    CHECK(b.timers == s.timers);
    CHECK(b.story == s.story);
    REQUIRE(b.story.asTable() != nullptr);
    CHECK(b.story.asTable()->array.size() == 2);
    CHECK_FALSE(b.story.asTable()->array[1].isInteger()); // 2.0 stays a float
    REQUIRE(b.hero.has_value());
    CHECK(b.hero->character == s.hero->character);
    CHECK(b.hero->position == s.hero->position);
    CHECK(b.hero->torch == "it_torch");
    REQUIRE(b.worlds.size() == 1);
    CHECK(b.worlds[0].delta.removed == s.worlds[0].delta.removed);
    CHECK(world::writeVobJson(b.worlds[0].delta.changed[0]) ==
          world::writeVobJson(s.worlds[0].delta.changed[0]));
    CHECK(b.worlds[0].delta.runtime[0].id == s.worlds[0].delta.runtime[0].id);
    CHECK(b.worlds[0].delta.nextRuntimeVobId == world::kRuntimeVobIdBase + 3);
    CHECK(b.worlds[0].spentTriggers == s.worlds[0].spentTriggers);
    CHECK(b.worlds[0].mobs == s.worlds[0].mobs);
    CHECK(b.worlds[0].npcs == s.worlds[0].npcs);
    CHECK(save::toJson(b) == text); // stable
}

TEST_CASE("Game state: broken documents and newer versions are refused")
{
    CHECK_FALSE(save::fromJson("[]").ok());
    CHECK_FALSE(save::fromJson("{\"version\": 1}").ok());
    std::string text = save::toJson(sample());
    const auto at = text.find("\"version\": 1");
    REQUIRE(at != std::string::npos);
    text.replace(at, 12, "\"version\": 99");
    auto newer = save::fromJson(text);
    REQUIRE_FALSE(newer.ok());
    CHECK(newer.error().message.find("newer") != std::string::npos);
    std::string wrongLife = save::toJson(sample());
    const auto life = wrongLife.find("\"dead\"");
    REQUIRE(life != std::string::npos);
    wrongLife.replace(life, 6, "\"zombie\"");
    auto zombie = save::fromJson(wrongLife);
    REQUIRE_FALSE(zombie.ok());
    CHECK(zombie.error().message.find("worlds[0].npcs[0].life") != std::string::npos);
}

TEST_CASE("World delta: removed, changed and new vobs; entries that no longer fit are dropped")
{
    world::WorldFile base;
    base.nextVobId = 10;
    base.vobs = {vob(1, "HUT", Vec3(0.0f)), vob(2, "BARREL", Vec3(1.0f, 0.0f, 0.0f)),
                 vob(3, "DOOR", Vec3(2.0f, 0.0f, 0.0f))};
    world::WorldFileVob child = vob(4, "HUT_ROOF", Vec3(0.0f, 3.0f, 0.0f));
    child.parent = world::VobId{1};
    base.vobs.push_back(child);

    world::WorldFile now = base;
    now.vobs.erase(now.vobs.begin() + 1);                    // the barrel is gone
    now.vobs[1].transform.position = Vec3(2.0f, 0.0f, 0.5f); // the door moved
    now.vobs.push_back(vob(10, "NEW", Vec3(5.0f)));          // a new world vob
    now.nextVobId = 11;
    const save::WorldDelta delta = save::diffWorld(base, now);
    REQUIRE(delta.removed.size() == 1);
    CHECK(delta.removed[0].name == "BARREL");
    REQUIRE(delta.changed.size() == 2);
    CHECK(delta.changed[0].name == "DOOR");
    CHECK(delta.changed[1].name == "NEW");
    CHECK(delta.nextVobId == 11);

    usize dropped = 99;
    const world::WorldFile applied = save::applyDelta(base, delta, &dropped);
    CHECK(dropped == 0);
    CHECK(save::diffWorld(applied, now).changed.empty());
    CHECK(save::diffWorld(applied, now).removed.empty());

    // The file was regenerated: ids 2 and 3 are other vobs now - neither is touched.
    world::WorldFile regenerated = base;
    regenerated.vobs[1].name = "CART";
    regenerated.vobs[2].name = "WINDOW";
    const world::WorldFile kept = save::applyDelta(regenerated, delta, &dropped);
    CHECK(dropped == 2);
    CHECK(kept.vobs.size() == 5); // nothing removed, the new vob added
    // A removed parent takes its children along.
    save::WorldDelta hutGone;
    hutGone.removed.push_back({world::VobId{1}, "HUT"});
    CHECK(save::applyDelta(base, hutGone).vobs.size() == 2);
}
