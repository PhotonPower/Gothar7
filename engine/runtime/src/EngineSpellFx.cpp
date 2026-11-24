// Spell glow (M17, plan "Magie-Nachträge" part G, decisions 9 and 10): who glows how - the hero and NPCs
// while they cast (the spell's [glow.<spell>] of data/spell_fx.toml), a flash when a spell hits them
// ([hit.<type>]), the burning and the sleeping ([state.*]). Handed to MeshRenderer::drawSkinned per figure.

#include "PlayerFigure.hpp"

#include <g7/core/Log.hpp>
#include <g7/runtime/Engine.hpp>

#include <cmath>

namespace g7
{
namespace
{
constexpr u32 kHeroId = ~u32(0);

/// A forearm: `lowerarm` and everything below it (hand, fingers), added to `mask`.
void addForearm(const animation::Skeleton& skeleton, std::string_view lowerarm, std::array<u32, 4>& mask)
{
    const i32 arm = skeleton.find(lowerarm);
    if (arm < 0)
    {
        return;
    }
    for (usize bone = 0; bone < skeleton.size() && bone < 128; ++bone)
    {
        for (i32 b = static_cast<i32>(bone); b >= 0; b = skeleton.parent(static_cast<usize>(b)))
        {
            if (b == arm)
            {
                mask[bone / 32] |= 1u << (bone % 32);
                break;
            }
        }
    }
}
} // namespace

void Engine::loadSpellFx()
{
    const std::string path = m_config.settings.get<std::string>("game.spell_fx", "data/spell_fx.toml");
    auto bytes = m_vfs.read(path);
    if (!bytes)
    {
        G7_LOG_WARN("engine", "{}: {} (no spell glow)", path, bytes.error().message);
        return;
    }
    auto parsed = render::SpellFxSettings::parse(
        std::string_view(reinterpret_cast<const char*>(bytes.value().data()), bytes.value().size()), path);
    if (!parsed)
    {
        G7_LOG_WARN("engine", "{} (no spell glow)", parsed.error().message);
        return;
    }
    m_spellFx = std::move(parsed).value();
}

void Engine::flashSpellHit(u32 targetId, const gameplay::DamageByType& damage)
{
    const render::GlowLook* strongest = nullptr;
    for (const auto& [type, amount] : damage)
    {
        const auto it = m_spellFx.hits.find(type);
        if (amount > 0 && it != m_spellFx.hits.end() &&
            (strongest == nullptr || it->second.fill + it->second.rim > strongest->fill + strongest->rim))
        {
            strongest = &it->second;
        }
    }
    if (strongest != nullptr)
    {
        m_glow[targetId].flash(*strongest);
    }
}

void Engine::fixedUpdateGlow(f32 seconds)
{
    for (auto it = m_glow.begin(); it != m_glow.end();)
    {
        it->second.update(seconds);
        it = it->second.flashing() ? std::next(it) : m_glow.erase(it);
    }
}

render::FigureGlow Engine::glowFor(u32 id, const gameplay::SpellInfo* casting, u32 stages, bool asleep) const
{
    const render::GlowLook* spell = nullptr;
    if (casting != nullptr)
    {
        const auto it = m_spellFx.spells.find(casting->instance);
        spell = it != m_spellFx.spells.end() ? &it->second : nullptr;
    }
    const render::GlowLook* state = m_burning.contains(id) ? &m_spellFx.burning
                                    : asleep               ? &m_spellFx.asleep
                                                           : nullptr;
    const auto flash = m_glow.find(id);
    static const render::FigureGlowState kNone;
    return (flash != m_glow.end() ? flash->second : kNone)
        .glow(spell, stages, state, static_cast<f32>(std::fmod(m_rainTime, 3600.0)));
}

render::FigureGlow Engine::heroGlow() const
{
    const bool casting = m_heroCast.has_value();
    return glowFor(kHeroId, casting ? &m_heroCast->spell : nullptr, casting ? m_heroCast->stages : 0, false);
}

render::FigureGlow Engine::creatureGlow(const Creature& c) const
{
    return glowFor(c.id, c.cast ? &c.cast->spell : nullptr, 0, c.asleep);
}

render::FigureGlow Engine::figureGlow(std::string_view who)
{
    if (who == "hero")
    {
        return heroGlow();
    }
    const Creature* c = npcNamed(who);
    return c != nullptr ? creatureGlow(*c) : render::FigureGlow{};
}

void Engine::applyGlowBones(AnimatedFigure& figure, render::FigureGlow& glow)
{
    if (!figure.glowBones)
    {
        std::array<std::array<u32, 4>, 2> arms{}; // right, left
        addForearm(figure.skeleton, "lowerarm_r", arms[0]);
        addForearm(figure.skeleton, "lowerarm_l", arms[1]);
        figure.glowBones = arms;
    }
    const auto& arms = *figure.glowBones;
    glow.handBones = {};
    for (usize w = 0; w < 4; ++w)
    {
        glow.handBones[w] = (glow.arm != render::GlowArm::Left ? arms[0][w] : 0u) |
                            (glow.arm != render::GlowArm::Right ? arms[1][w] : 0u);
    }
}
} // namespace g7
