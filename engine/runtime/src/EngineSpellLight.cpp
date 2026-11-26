// The light spell (owner's wish 2026-11-26: "eine Art Plasmasphäre, die Licht macht und auf der Hand
// schwebt"): casting spl_light (kind "light") puts an orb of pale blue-white plasma about 15 cm over the
// hero's open left hand, with a point light of about 8 m and a quiet hum. While that hand is busy - a
// two-handed weapon, a bow or crossbow, a rune or scroll, the torch, climbing, swimming, using a mob - the
// orb hovers over the left shoulder and comes back afterwards. It lasts the Spell's duration in simulation
// time (it stands still in the pause); casting it again renews it. In the last seconds it flickers more
// strongly, then it fades out (no hum then) and goes with a "pop". The look and the numbers are the Spell's
// `light`, `fx` and `sounds` fields (magic/spells.lua).
//
// Runs on the main thread in the fixed step, after the creatures (never in their parallel animation phase).

#include "PlayerFigure.hpp"

#include <g7/core/Log.hpp>
#include <g7/runtime/Engine.hpp>

#include <cmath>

namespace g7
{
namespace
{
using script::Value;

constexpr std::string_view kHandBone = "socket_hand_l";
constexpr std::string_view kShoulderBone = "upperarm_l";
constexpr f32 kShoulderRate = 3.0f; ///< per second: hand <-> shoulder in about a third of a second
/// The left forearm raised in front, the hand at chest height: the torch's carrying pose for now (figuren may
/// give the light an open hand of its own).
constexpr std::string_view kHoldClip = "none/a_torch_hold";

/// 0..1 flicker of the light at `time` (no random numbers: the same run every time).
f32 flickerWave(f32 time) noexcept
{
    return 0.5f + 0.5f * (0.6f * std::sin(time * 13.0f) + 0.4f * std::sin(time * 29.3f + 1.1f));
}

std::string fieldOf(const std::map<std::string, std::string, std::less<>>& map, std::string_view key)
{
    const auto it = map.find(key);
    return it != map.end() ? it->second : std::string();
}
} // namespace

f32 Engine::lightSecondsLeft() const noexcept
{
    return m_lightOrb ? m_lightOrb->seconds : 0.0f;
}

std::optional<Vec3> Engine::lightOrbPosition() const
{
    return m_lightOrb ? std::optional<Vec3>(m_lightOrb->position) : std::nullopt;
}

bool Engine::lightOrbAtShoulder() const noexcept
{
    return m_lightOrb && m_lightOrb->shoulder > 0.5f;
}

std::optional<render::PointLight> Engine::lightOrbLight() const
{
    if (!m_lightOrb)
    {
        return std::nullopt;
    }
    const LightOrb& o = *m_lightOrb;
    // Gentle flicker, stronger in the last seconds; the fade dims it to nothing.
    f32 amplitude = o.look.flicker;
    if (o.fade > 0.0f || o.seconds < o.look.warn)
    {
        const f32 warn = o.look.warn > 0.0f ? 1.0f - std::clamp(o.seconds / o.look.warn, 0.0f, 1.0f) : 1.0f;
        amplitude = glm::mix(o.look.flicker, o.look.warnFlicker, warn);
    }
    f32 intensity = o.look.intensity * (1.0f - amplitude * flickerWave(o.time));
    if (o.fade > 0.0f)
    {
        intensity *= o.look.fade > 0.0f ? std::clamp(o.fade / o.look.fade, 0.0f, 1.0f) : 0.0f;
    }
    render::PointLight light;
    light.position = o.position;
    light.radius = o.look.range;
    light.color = o.look.color;
    light.intensity = std::max(intensity, 0.0f);
    return light;
}

bool Engine::heroLeftHandBusy() const
{
    if (m_weaponMode >= 3) // bow or crossbow, a rune or scroll, an animal's shape
    {
        return true;
    }
    if (m_weaponMode == 1 && m_scripts)
    {
        const script::Instance* weapon = m_scripts->findInstance("Item", m_weaponDrawn);
        if (weapon != nullptr && weapon->fields["category"].asString() == "melee_2h")
        {
            return true;
        }
    }
    return m_torch.has_value() || m_climb.has_value() || m_mobUse.has_value() ||
           m_swimmer.mode() != gameplay::WaterMode::Land;
}

void Engine::castLight(const gameplay::SpellInfo& spell)
{
    startLight(spell, spell.duration > 0.0f ? spell.duration : 300.0f);
}

void Engine::startLight(const gameplay::SpellInfo& spell, f32 seconds)
{
    const bool renewed = m_lightOrb.has_value();
    if (!renewed)
    {
        LightOrb o;
        o.shoulder = heroLeftHandBusy() ? 1.0f : 0.0f;
        m_lightOrb = std::move(o);
    }
    LightOrb& o = *m_lightOrb;
    o.spell = spell.instance;
    o.look = spell.light;
    o.orbFx = fieldOf(spell.fx, "orb");
    o.haloFx = fieldOf(spell.fx, "halo");
    o.sparksFx = fieldOf(spell.fx, "sparks");
    o.humSound = fieldOf(spell.sounds, "loop");
    o.fadeSound = fieldOf(spell.sounds, "fade");
    o.seconds = seconds;
    o.fade = 0.0f;          // renewed while fading: bright again
    fixedUpdateLight(0.0f); // in place at once, effects and hum (re)started
    if (m_scripts)
    {
        const Value args[] = {o.spell, Value(static_cast<f64>(seconds))};
        m_scripts->emit("light_on", args);
    }
    G7_LOG_DEBUG("engine", "light {} for {:.0f} s{}", o.spell, seconds, renewed ? " (renewed)" : "");
}

void Engine::stopLight()
{
    if (!m_lightOrb)
    {
        return;
    }
    for (const auto& fx : {m_lightOrb->orb, m_lightOrb->halo, m_lightOrb->sparks})
    {
        if (fx)
        {
            m_particles.stop(*fx);
        }
    }
    if (m_lightOrb->hum && m_audio)
    {
        m_audio->stop(*m_lightOrb->hum, 0.1f);
        m_spatialSounds.erase(*m_lightOrb->hum);
    }
    if (m_lightOrb->holding && m_figure && !m_torch && m_figure->animator.overlayClip() == kHoldClip)
    {
        m_figure->animator.stopOverlay(0.3f);
    }
    m_lightOrb.reset();
}

void Engine::fixedUpdateLight(f32 seconds)
{
    if (!m_lightOrb)
    {
        return;
    }
    LightOrb& o = *m_lightOrb;
    o.time += seconds;
    if (o.fade > 0.0f)
    {
        o.fade -= seconds;
        if (o.fade <= 0.0f)
        {
            const std::string spell = o.spell;
            stopLight();
            if (m_scripts)
            {
                const Value args[] = {spell};
                m_scripts->emit("light_out", args);
            }
            return;
        }
    }
    else
    {
        o.seconds = std::max(o.seconds - seconds, 0.0f);
        if (o.seconds <= 0.0f && seconds > 0.0f)
        {
            // Its time is up: no more hum or sparks, the orb's last particles die, the light fades, a "pop".
            o.fade = std::max(o.look.fade, 1e-3f);
            for (auto* fx : {&o.orb, &o.halo, &o.sparks})
            {
                if (*fx)
                {
                    m_particles.stop(**fx);
                    fx->reset();
                }
            }
            if (o.hum && m_audio)
            {
                m_audio->stop(*o.hum, 0.15f);
                m_spatialSounds.erase(*o.hum);
                o.hum.reset();
            }
            if (!o.fadeSound.empty())
            {
                (void)playSound(o.fadeSound, o.position);
            }
        }
    }

    // Over the open hand, or over the shoulder while the hand is busy; eased between the two.
    const bool busy = heroLeftHandBusy();
    const f32 target = busy ? 1.0f : 0.0f;
    // The hand held up in front while it is free (as the torch's pose; a dialogue gesture may take its place
    // for a while, then it comes back). Not while fading, nor when something else holds the arm.
    if (m_figure && m_figure->animator.hasClip(kHoldClip))
    {
        const bool want = !busy && o.fade <= 0.0f && m_weaponMode != 5;
        const std::string_view overlay = m_figure->animator.overlayClip();
        if (want && overlay.empty())
        {
            m_figure->animator.playOverlay(kHoldClip, "clavicle_l", 0.3f, true, "none/a_neutral", true);
            o.holding = true;
        }
        else if (!want && o.holding)
        {
            if (overlay == kHoldClip && !m_torch)
            {
                m_figure->animator.stopOverlay(0.3f);
            }
            o.holding = false;
        }
    }
    const f32 step = kShoulderRate * seconds;
    o.shoulder += std::clamp(target - o.shoulder, -step, step);
    if (seconds == 0.0f)
    {
        o.shoulder = target; // starting: where it belongs
    }
    const Vec3 feet =
        m_player.valid() ? m_player.feet() : m_camera.transform.position - Vec3(0.0f, 1.7f, 0.0f);
    const auto hand = playerSocketTransform(kHandBone);
    const auto shoulder = playerSocketTransform(kShoulderBone);
    const Vec3 overHand =
        (hand ? Vec3((*hand)[3]) : feet + Vec3(0.0f, 1.3f, 0.0f)) + Vec3(0.0f, o.look.hover, 0.0f);
    const Vec3 overShoulder =
        (shoulder ? Vec3((*shoulder)[3]) : feet + Vec3(0.0f, 1.5f, 0.0f)) + Vec3(0.0f, o.look.shoulder, 0.0f);
    const f32 s = o.shoulder * o.shoulder * (3.0f - 2.0f * o.shoulder);
    const Vec3 wobble(0.012f * std::sin(o.time * 1.7f), 0.018f * std::sin(o.time * 2.3f),
                      0.012f * std::sin(o.time * 1.3f + 1.0f));
    o.position = glm::mix(overHand, overShoulder, s) + wobble;

    if (o.fade > 0.0f)
    {
        return; // fading: nothing (re)started
    }
    // The effects and the hum follow it; started anew if they are gone.
    const auto follow = [&](std::optional<u32>& fx, const std::string& name)
    {
        if (name.empty())
        {
            return;
        }
        if (fx && m_particles.alive(*fx))
        {
            m_particles.move(*fx, o.position, Vec3(0.0f, 1.0f, 0.0f));
        }
        else
        {
            fx = startEffect(name, o.position);
        }
    };
    follow(o.orb, o.orbFx);
    follow(o.halo, o.haloFx);
    follow(o.sparks, o.sparksFx);
    if (m_audio && !o.humSound.empty())
    {
        if (o.hum && m_audio->playing(*o.hum))
        {
            m_audio->setPosition(*o.hum, o.position);
            m_spatialSounds[*o.hum] = o.position;
        }
        else
        {
            o.hum = playSound(o.humSound, o.position);
        }
    }
}

void Engine::bindLightFunctions()
{
    script::ScriptVm& vm = *m_scripts;
    vm.bind({"hero_light_seconds", "hero_light_seconds() -> number",
             "Wie viele Sekunden die Lichtkugel des Helden noch leuchtet (0: keine).", "Magie",
             [this](std::span<const Value>) -> Result<Value>
             { return Value(static_cast<f64>(lightSecondsLeft())); }});
    vm.bind({"light_on",
             "on(\"light_on\", fn(spell: string, seconds: number))",
             "Der Held hat einen Lichtzauber gewirkt (auch erneuert); `seconds` ist seine Dauer.",
             "Ereignisse",
             {}});
    vm.bind({"light_out",
             "on(\"light_out\", fn(spell: string))",
             "Die Lichtkugel des Helden ist verloschen.",
             "Ereignisse",
             {}});
}
} // namespace g7
