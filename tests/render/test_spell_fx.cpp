// Spell glow (M17 part G, decisions 9 and 10): data/spell_fx.toml, the glow while casting (stronger per
// stage), the flash of a hit fading out, the pulse of the burning, all of them added up.

#include <g7/core/FileSystem.hpp>
#include <g7/render/SpellFx.hpp>

#include <doctest/doctest.h>

#include <ostream>
#include <string>

using namespace g7;
using namespace g7::render;

namespace
{
SpellFxSettings shipped()
{
    const auto text = fs::readText(fs::fromUtf8(G7_ASSET_SOURCE_DIR) / "data" / "spell_fx.toml");
    REQUIRE(text.ok());
    auto parsed = SpellFxSettings::parse(text.value(), "data/spell_fx.toml");
    REQUIRE_MESSAGE(parsed.ok(), (parsed.ok() ? "" : parsed.error().message));
    return std::move(parsed).value();
}

std::string errorOf(std::string_view toml)
{
    auto parsed = SpellFxSettings::parse(toml, "spell_fx.toml");
    return parsed.ok() ? std::string() : parsed.error().message;
}
} // namespace

TEST_CASE("Spell fx: the shipped data - fire orange, healing green, sleep, fear and telekinesis blue")
{
    const SpellFxSettings s = shipped();
    for (const char* spell : {"spl_firebolt", "spl_heal", "spl_sleep", "spl_fear", "spl_telekinesis"})
    {
        REQUIRE_MESSAGE(s.spells.contains(spell), spell);
        CHECK(s.spells.find(spell)->second.hand > s.spells.find(spell)->second.rim); // decision 9: the hand
    }
    // The arm the cast clip raises: left for bolt, target and loop clips, both for the self clip.
    CHECK(s.spells.find("spl_firebolt")->second.arm == GlowArm::Left);
    CHECK(s.spells.find("spl_heal")->second.arm == GlowArm::Both);
    CHECK(s.spells.find("spl_telekinesis")->second.arm == GlowArm::Left);
    const Vec3 fire = s.spells.find("spl_firebolt")->second.color;
    CHECK(fire.r > fire.b);
    CHECK(s.spells.find("spl_heal")->second.color.g > s.spells.find("spl_heal")->second.color.r);
    CHECK(s.spells.find("spl_sleep")->second.color.b > s.spells.find("spl_sleep")->second.color.r);
    // Decision 10: a hit flashes 0.3 s in its damage's colour.
    REQUIRE(s.hits.contains("fire"));
    REQUIRE(s.hits.contains("magic"));
    CHECK(s.hits.find("fire")->second.seconds == doctest::Approx(0.3f));
    CHECK(s.burning.pulse > 0.0f);
    CHECK(s.asleep.rim > 0.0f);
    CHECK(s.asleep.rim < s.burning.rim); // a faint shimmer
}

TEST_CASE("Spell fx: errors name the key")
{
    CHECK(errorOf("version = 2").find("version") != std::string::npos);
    CHECK(errorOf("[glow.spl_x]\nhand = 1\n").find("glow.spl_x.color") != std::string::npos);
    CHECK(errorOf("[glow.spl_x]\ncolor = [1, 0, 0]\nhand = -1\n").find("glow.spl_x.hand") !=
          std::string::npos);
    CHECK(errorOf("[state.burning]\ncolor = [1, 0]\n").find("state.burning.color") != std::string::npos);
    CHECK(errorOf("[glow.spl_x]\narm = \"up\"\ncolor = [1, 0, 0]\n").find("glow.spl_x.arm") !=
          std::string::npos);
    CHECK(SpellFxSettings::parse("[glow.a]\ncolor = [1, 0, 0]\n", "x").value().spells.at("a").arm ==
          GlowArm::Left); // the rune's hand
    CHECK(errorOf("").empty());
}

TEST_CASE("Spell fx: casting glows at the hand, stronger per stage; a flash fades; states add up")
{
    GlowLook cast{.color = Vec3(1.0f, 0.5f, 0.0f), .hand = 2.0f, .rim = 0.2f, .perStage = 0.5f};
    GlowLook hit{.color = Vec3(0.0f, 0.0f, 1.0f), .rim = 1.0f, .fill = 0.4f, .seconds = 0.3f};
    GlowLook burning{.color = Vec3(1.0f, 0.3f, 0.0f), .rim = 1.0f, .pulse = 2.0f};
    FigureGlowState state;
    CHECK_FALSE(state.glow(nullptr, 0, nullptr, 0.0f).active());

    const FigureGlow casting = state.glow(&cast, 0, nullptr, 0.0f);
    CHECK(casting.hand.r == doctest::Approx(2.0f));
    CHECK(casting.rim.g == doctest::Approx(0.1f));
    CHECK(casting.fill == Vec3(0.0f));
    CHECK(casting.arm == GlowArm::Left);
    GlowLook bothArms = cast;
    bothArms.arm = GlowArm::Both;
    CHECK(state.glow(&bothArms, 0, nullptr, 0.0f).arm == GlowArm::Both);
    CHECK(state.glow(&cast, 2, nullptr, 0.0f).hand.r == doctest::Approx(4.0f)); // two stages: twice as much

    state.flash(hit);
    CHECK(state.flashing());
    CHECK(state.glow(nullptr, 0, nullptr, 0.0f).fill.b == doctest::Approx(0.4f));
    state.update(0.15f);
    CHECK(state.glow(nullptr, 0, nullptr, 0.0f).fill.b == doctest::Approx(0.2f)); // half way: half
    state.flash(
        GlowLook{.color = Vec3(1.0f), .rim = 0.05f, .seconds = 0.3f}); // weaker than what is left: kept
    CHECK(state.glow(nullptr, 0, nullptr, 0.0f).fill.b == doctest::Approx(0.2f));
    state.update(0.2f);
    CHECK_FALSE(state.flashing());
    CHECK_FALSE(state.glow(nullptr, 0, nullptr, 0.0f).active());

    // The burning pulses between 0.5 and 1.
    f32 low = 10.0f;
    f32 high = 0.0f;
    for (f32 t = 0.0f; t < 1.0f; t += 0.01f)
    {
        const f32 r = state.glow(nullptr, 0, &burning, t).rim.r;
        low = std::min(low, r);
        high = std::max(high, r);
    }
    CHECK(low == doctest::Approx(0.5f).epsilon(0.02));
    CHECK(high == doctest::Approx(1.0f).epsilon(0.02));
    // Casting while burning: both.
    const FigureGlow both = state.glow(&cast, 0, &burning, 0.0f);
    CHECK(both.hand.r == doctest::Approx(2.0f));
    CHECK(both.rim.r > 0.2f);
}
