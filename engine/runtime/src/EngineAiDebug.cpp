// NPC behaviour for developers (M9 part E, docs/modules/ai.md "Debug"): the window "AI" with every NPC's
// state, routine, command queue and perception, and in the overlay (F2) the senses of the NPC selected there.

#include "PlayerFigure.hpp"

#include <g7/core/Log.hpp>
#include <g7/gameplay/Movement.hpp>
#include <g7/runtime/Engine.hpp>

#include <cmath>
#include <format>

namespace g7
{
namespace
{
std::string describe(const Creature::Command& cmd)
{
    using Kind = Creature::Command::Kind;
    switch (cmd.kind)
    {
    case Kind::GoTo:
        return std::format("goto {}{}", cmd.text, cmd.run ? " (run)" : "");
    case Kind::GoToFreepoint:
        return std::format("goto freepoint {} within {:.0f} m", cmd.text, cmd.value);
    case Kind::Turn:
        return std::format("turn to {}", cmd.text);
    case Kind::Play:
        return std::format("play {}{}", cmd.text, cmd.item.empty() ? "" : " with " + cmd.item);
    case Kind::Stop:
        return "stop";
    case Kind::Wait:
        return std::format("wait {:.1f} s", cmd.value);
    case Kind::Say:
        return std::format("say \"{}\"", cmd.text);
    case Kind::Follow:
        return std::format("follow {} for {:.0f} s", cmd.text, cmd.value);
    case Kind::Flee:
        return std::format("flee from {} for {:.0f} s", cmd.text.empty() ? "@player" : cmd.text, cmd.value);
    case Kind::GoToPoint:
        return std::format("goto ({:.1f}, {:.1f}, {:.1f})", cmd.point.x, cmd.point.y, cmd.point.z);
    case Kind::Roam:
        return std::format("roam {:.0f} m around {}", cmd.value, cmd.text);
    }
    return "?";
}
} // namespace

ui::AiPanel Engine::aiPanelData()
{
    ui::AiPanel panel;
    panel.filter = m_aiFilter;
    panel.selected = m_aiSelected;
    panel.scriptErrors = m_scripts ? m_scripts->callErrors() : 0;
    const Vec3 eye = m_player.valid() ? m_player.feet() : m_camera.transform.position;
    for (const auto& owned : m_creatures)
    {
        const Creature& c = *owned;
        if (!c.character)
        {
            continue; // showcase creatures (M6)
        }
        ui::AiPanel::Row row;
        row.id = c.id;
        row.name = c.species;
        row.state = c.state;
        row.routine = c.routine;
        row.at = c.stateAt;
        row.ambient = c.ambient;
        row.animation = c.figure ? std::string(c.figure->animator.state()) : std::string();
        for (const Creature::Command& cmd : c.commands)
        {
            row.commands.push_back(describe(cmd));
        }
        row.simulated = c.simulated;
        row.seesPlayer = c.seesPlayer;
        row.walking = c.route.has_value();
        row.distance = glm::length(c.position - eye);
        if (m_scripts)
        {
            const script::Value args[] = {c.species};
            if (auto attitude = m_scripts->callGlobal("npc_attitude", args))
            {
                row.attitude = std::string(attitude.value().asString());
            }
        }
        panel.rows.push_back(std::move(row));
    }
    return panel;
}

void Engine::aiUi()
{
    ui::AiPanel panel = aiPanelData();
    m_debugUi.aiPanel(panel);
    m_aiFilter = panel.filter;
    m_aiSelected = panel.selected;
}

void Engine::drawAiSenses()
{
    const Creature* c = m_aiSelected != 0 ? creature(m_aiSelected) : nullptr;
    if (c == nullptr || !c->character)
    {
        return;
    }
    // Sight: the cone's edges and its far arc at eye height; a line to the player while seen.
    const Vec3 eye = c->position + Vec3(0.0f, 1.6f, 0.0f);
    const f32 half = std::acos(std::clamp(c->sightCos, -1.0f, 1.0f));
    const render::DebugStyle sight{c->seesPlayer ? Vec4(0.3f, 1.0f, 0.3f, 0.9f)
                                                 : Vec4(1.0f, 1.0f, 0.4f, 0.7f)};
    constexpr int kSegments = 16;
    Vec3 previous = eye;
    for (int i = 0; i <= kSegments; ++i)
    {
        const f32 yaw = c->yaw - half + 2.0f * half * static_cast<f32>(i) / kSegments;
        const Vec3 p = eye + gameplay::forwardOf(yaw) * c->sight;
        m_debugDraw.line(previous, p, sight);
        previous = p;
    }
    m_debugDraw.line(previous, eye, sight);
    if (c->seesPlayer && m_player.valid())
    {
        m_debugDraw.line(eye, m_player.feet() + Vec3(0.0f, 1.2f, 0.0f), sight);
    }
    // Hearing: how far running is heard.
    if (const f32 run = noiseRadius("run") * c->hearing; run > 0.0f)
    {
        m_debugDraw.circle(c->position + Vec3(0.0f, 0.1f, 0.0f), Vec3(0.0f, 1.0f, 0.0f), run,
                           render::DebugStyle{Vec4(0.4f, 0.7f, 1.0f, 0.6f)});
    }
    m_debugDraw.text(eye + Vec3(0.0f, 0.5f, 0.0f),
                     std::format("{} {}", c->species, c->state.empty() ? "-" : c->state),
                     render::DebugStyle{Vec4(1.0f)});
}
} // namespace g7
