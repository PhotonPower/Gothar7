#include <g7/core/Config.hpp>
#include <g7/render/SpellFx.hpp>

#include <algorithm>
#include <cmath>
#include <format>

namespace g7::render
{
namespace
{
Result<GlowLook> readLook(const Config& c, const std::string& at, std::string_view source)
{
    GlowLook look;
    const std::string arm = c.get<std::string>(at + ".arm", "right");
    if (arm != "right" && arm != "left" && arm != "both")
    {
        return Error{std::format("{}: {}.arm: right, left or both, not \"{}\"", source, at, arm)};
    }
    look.arm = arm == "left" ? GlowArm::Left : arm == "both" ? GlowArm::Both : GlowArm::Right;
    const std::string colour = at + ".color";
    if (c.arraySize(colour) != 3)
    {
        return Error{std::format("{}: {}: needs [r, g, b]", source, colour)};
    }
    for (int i = 0; i < 3; ++i)
    {
        const auto v = c.find<f64>(std::format("{}[{}]", colour, i));
        if (!v || *v < 0.0 || *v > 100.0)
        {
            return Error{std::format("{}: {}: numbers 0..100", source, colour)};
        }
        look.color[i] = static_cast<f32>(*v);
    }
    for (auto [key, target] : {std::pair{"hand", &look.hand}, std::pair{"rim", &look.rim},
                               std::pair{"fill", &look.fill}, std::pair{"per_stage", &look.perStage},
                               std::pair{"seconds", &look.seconds}, std::pair{"pulse", &look.pulse}})
    {
        const std::string name = at + "." + key;
        if (!c.contains(name))
        {
            continue;
        }
        const auto v = c.find<f64>(name);
        if (!v || *v < 0.0 || *v > 100.0)
        {
            return Error{std::format("{}: {}: a number 0..100", source, name)};
        }
        *target = static_cast<f32>(*v);
    }
    return look;
}
} // namespace

Result<SpellFxSettings> SpellFxSettings::parse(std::string_view toml, std::string_view source)
{
    auto parsed = Config::parse(toml, source);
    if (!parsed)
    {
        return parsed.error();
    }
    const Config& c = parsed.value();
    if (c.get<i64>("version", 1) != 1)
    {
        return Error{std::format("{}: version: must be 1", source)};
    }
    SpellFxSettings s;
    for (const auto& [table, target] : {std::pair{"glow", &s.spells}, std::pair{"hit", &s.hits}})
    {
        for (const std::string& name : c.keys(table))
        {
            auto look = readLook(c, std::format("{}.{}", table, name), source);
            if (!look)
            {
                return look.error();
            }
            target->emplace(name, look.value());
        }
    }
    for (const auto& [name, target] : {std::pair{"burning", &s.burning}, std::pair{"asleep", &s.asleep}})
    {
        if (c.contains(std::format("state.{}", name)))
        {
            auto look = readLook(c, std::format("state.{}", name), source);
            if (!look)
            {
                return look.error();
            }
            *target = look.value();
        }
    }
    return s;
}

void FigureGlowState::flash(const GlowLook& look) noexcept
{
    if (look.seconds <= 0.0f)
    {
        return;
    }
    // What is left of the current flash; a new one at least as strong replaces it.
    const f32 left = m_left > 0.0f ? (m_flash.fill + m_flash.rim) * (m_left / m_flash.seconds) : 0.0f;
    if (look.fill + look.rim >= left)
    {
        m_flash = look;
        m_left = look.seconds;
    }
}

void FigureGlowState::update(f32 seconds) noexcept
{
    m_left = std::max(0.0f, m_left - seconds);
}

FigureGlow FigureGlowState::glow(const GlowLook* casting, u32 stages, const GlowLook* state,
                                 f32 time) const noexcept
{
    FigureGlow g;
    if (casting != nullptr)
    {
        const f32 grow = 1.0f + casting->perStage * static_cast<f32>(stages);
        g.hand += casting->color * (casting->hand * grow);
        g.arm = casting->arm;
        g.rim += casting->color * (casting->rim * grow);
        g.fill += casting->color * casting->fill;
    }
    if (state != nullptr)
    {
        const f32 pulse =
            state->pulse > 0.0f ? 0.75f + 0.25f * std::sin(time * state->pulse * 6.2831853f) : 1.0f;
        g.rim += state->color * (state->rim * pulse);
        g.fill += state->color * (state->fill * pulse);
    }
    if (m_left > 0.0f)
    {
        const f32 f = m_left / m_flash.seconds; // fades out linearly
        g.rim += m_flash.color * (m_flash.rim * f);
        g.fill += m_flash.color * (m_flash.fill * f);
    }
    return g;
}
} // namespace g7::render
