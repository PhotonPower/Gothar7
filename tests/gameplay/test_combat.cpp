// Combat (M11 part A): damage like Gothic 1 (K2, K3), the fighter's state machine with combos by talent (K4),
// parry (K6), knock-out (K7); the values of data/combat.lua.

#include <g7/gameplay/Combat.hpp>
#include <g7/gameplay/Movement.hpp>
#include <g7/script/ScriptVm.hpp>

#include <doctest/doctest.h>

#include <fstream>
#include <ostream> // doctest needs it to print std::string operands
#include <sstream>
#include <string>

using namespace g7;
using namespace g7::gameplay;

namespace
{
const auto noProtection = [](std::string_view) { return 0; };
} // namespace

TEST_CASE("Combat damage: weapon + strength - protection, at least the minimum, critical by talent")
{
    const CombatSettings s;
    // Old sword edge 18, strength 20, no protection: 38.
    CHECK(meleeDamage({{"edge", 18}}, 20, noProtection, 0, 0.5f, s).damage == 38);
    // Edge protection 30: 8.
    const auto armour = [](std::string_view type) { return type == "edge" ? 30 : 0; };
    CHECK(meleeDamage({{"edge", 18}}, 20, armour, 0, 0.5f, s).damage == 8);
    // Protection above everything: the minimum (K2: 5).
    const auto heavy = [](std::string_view) { return 100; };
    CHECK(meleeDamage({{"edge", 18}}, 20, heavy, 0, 0.5f, s).damage == 5);
    // Fists: blunt 0 + strength.
    CHECK(meleeDamage({}, 12, noProtection, 0, 0.5f, s).damage == 12);
    // Strength goes to the main type only; each type against its own protection.
    const auto blunt = [](std::string_view type) { return type == "blunt" ? 10 : 0; };
    CHECK(meleeDamage({{"edge", 10}, {"blunt", 4}}, 10, blunt, 0, 0.5f, s).damage == 20); // edge 20, blunt 0

    // K3: critical by talent - 0 % at level 0, 10 % at 1, 20 % at 2; doubles the weapon's damage.
    CHECK_FALSE(meleeDamage({{"edge", 18}}, 20, noProtection, 0, 0.0f, s).critical);
    CHECK(meleeDamage({{"edge", 18}}, 20, noProtection, 1, 0.05f, s).critical);
    CHECK_FALSE(meleeDamage({{"edge", 18}}, 20, noProtection, 1, 0.15f, s).critical);
    const DamageResult crit = meleeDamage({{"edge", 18}}, 20, noProtection, 2, 0.15f, s);
    CHECK(crit.critical);
    CHECK(crit.damage == 56); // 2 * 18 + 20
}

TEST_CASE("Combat: the parry faces the attacker within its angle")
{
    // Looking along -Z (yaw 0): an attacker at -Z is in front, one at +X (90 degrees) is not within 60.
    CHECK(facesAttacker(0.0f, Vec2(0.0f), Vec2(0.0f, -2.0f), 60.0f));
    CHECK(facesAttacker(0.0f, Vec2(0.0f), Vec2(1.0f, -2.0f), 60.0f));      // 27 degrees
    CHECK_FALSE(facesAttacker(0.0f, Vec2(0.0f), Vec2(2.0f, 0.0f), 60.0f)); // 90
    CHECK_FALSE(facesAttacker(0.0f, Vec2(0.0f), Vec2(0.0f, 2.0f), 60.0f)); // behind
}

TEST_CASE("Fighter: attacks with a hit window, combos by talent, the timeline without clips")
{
    const CombatSettings s;
    const auto run = [&](Fighter& f, f32 seconds)
    {
        for (f32 t = 0.0f; t < seconds; t += 0.01f)
        {
            f.update(0.01f, s);
        }
    };
    SUBCASE("talent 0: single hits, no combo")
    {
        Fighter f;
        REQUIRE(f.attack(AttackKind::Front, 0, s));
        f.useTimeline();
        CHECK(f.clip("1h") == "1h/t_attack_combo1");
        CHECK(f.takeNewSwing());
        CHECK_FALSE(f.takeNewSwing());
        CHECK_FALSE(f.hitWindow());
        run(f, 0.3f);
        CHECK(f.hitWindow());
        CHECK_FALSE(f.attack(AttackKind::Front, 0, s)); // busy
        run(f, 0.25f);
        CHECK_FALSE(f.hitWindow());
        CHECK_FALSE(f.attack(AttackKind::Front, 0, s)); // no combo at talent 0
        run(f, 0.5f);
        CHECK(f.state() == FightState::Ready);
    }
    SUBCASE("talent 1: up to three hits, only within the combo window")
    {
        Fighter f;
        REQUIRE(f.attack(AttackKind::Front, 1, s));
        f.useTimeline();
        for (u32 hit = 2; hit <= 3; ++hit)
        {
            run(f, 0.5f); // past hit_end: the combo window is open
            REQUIRE(f.attack(AttackKind::Front, 1, s));
            f.useTimeline();
            CHECK(f.comboHit() == hit);
            CHECK(f.clip("fist") == std::format("fist/t_attack_combo{}", hit));
        }
        run(f, 0.5f);
        CHECK_FALSE(f.attack(AttackKind::Front, 1, s)); // three is the most at talent 1
        run(f, 0.5f);
        CHECK(f.state() == FightState::Ready);
        // Too late (after combo_end): no next hit.
        REQUIRE(f.attack(AttackKind::Front, 1, s));
        f.useTimeline();
        run(f, 0.75f);
        CHECK_FALSE(f.attack(AttackKind::Front, 1, s));
    }
    SUBCASE("talent 2: four hits and faster")
    {
        Fighter f;
        REQUIRE(f.attack(AttackKind::Front, 2, s));
        CHECK(f.rate() == doctest::Approx(1.25f));
    }
    SUBCASE("clip events drive it when the clip is there")
    {
        Fighter f;
        REQUIRE(f.attack(AttackKind::Left, 2, s));
        CHECK(f.clip("1h") == "1h/t_attack_l");
        run(f, 2.0f); // no timeline: waits for the clip
        CHECK(f.state() == FightState::Attack);
        f.onEvent("hit_start");
        CHECK(f.hitWindow());
        f.onEvent("hit_end");
        f.onEvent("combo_start");
        CHECK_FALSE(f.attack(AttackKind::Front, 2, s)); // side hits do not chain
        f.onClipDone();
        CHECK(f.state() == FightState::Ready);
    }
}

TEST_CASE("Fighter: parry window, stagger, knocked out and standing up, dead")
{
    const CombatSettings s;
    Fighter f;
    REQUIRE(f.parry());
    f.useTimeline();
    CHECK(f.clip("1h") == "1h/t_parry");
    f.update(0.3f, s);
    CHECK(f.parrying(s));
    f.update(0.15f, s);
    CHECK_FALSE(f.parrying(s)); // K6: 0.4 s
    f.update(0.2f, s);
    CHECK(f.state() == FightState::Ready);

    REQUIRE(f.attack(AttackKind::Front, 0, s));
    f.stagger(); // hit while swinging
    CHECK(f.state() == FightState::Stagger);
    CHECK(f.clip("1h") == "none/t_hit_light");
    f.useTimeline();
    f.update(0.6f, s);
    CHECK(f.state() == FightState::Ready);

    f.knockOut(s.knockoutSeconds);
    CHECK(f.clip("fist") == "none/t_ko");
    f.stagger(); // a hit on the one lying does not make him stagger
    CHECK(f.state() == FightState::Down);
    f.update(29.0f, s);
    CHECK(f.state() == FightState::Down);
    f.update(1.5f, s);
    CHECK(f.state() == FightState::Ready); // K7: 30 s

    f.die();
    CHECK_FALSE(f.attack(AttackKind::Front, 0, s));
    f.update(100.0f, s);
    CHECK(f.state() == FightState::Dead);
}

TEST_CASE("Combat values: data/combat.lua")
{
    auto vm = script::ScriptVm::create({});
    REQUIRE(vm.ok());
    std::ifstream file(G7_SCRIPT_DIR "/data/combat.lua");
    REQUIRE(file.good());
    std::stringstream text;
    text << file.rdbuf();
    REQUIRE(vm.value().runString(text.str(), "combat.lua").ok());
    const script::Value table = vm.value().global("Combat");
    REQUIRE(table.asTable() != nullptr);
    auto s = CombatSettings::fromTable(*table.asTable());
    REQUIRE_MESSAGE(s.ok(), (s.ok() ? "" : s.error().message));
    CHECK(s.value().minDamage == 5);
    CHECK(s.value().comboHits[2] == 4);
    CHECK(s.value().critChance[1] == doctest::Approx(0.1f));

    auto bad = script::makeTable({}, {{"combo_hits", script::makeTable({1.0, 2.0})}});
    CHECK_FALSE(CombatSettings::fromTable(*bad.asTable()).ok());
}
