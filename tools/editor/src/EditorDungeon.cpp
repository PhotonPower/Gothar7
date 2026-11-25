// Dungeon parts in the editor (dungeon D7, docs/modules/dungeon.md "Editor"): inspector fields of the event
// net's components, lines from senders to their targets, previews of moving parts.

#include <g7/editor/Editor.hpp>
#include <g7/render/DebugDraw.hpp>
#include <g7/world/Scene.hpp>
#include <g7/world/WorldScene.hpp>

#include <algorithm>
#include <array>
#include <format>

namespace g7::editor
{
namespace
{
constexpr f32 kLinkDrawRadius = 80.0f; ///< metres from the camera to a line's sender
constexpr Vec3 kLinkLift(0.0f, 0.3f, 0.0f);

constexpr std::array<std::string_view, 3> kMoverKinds = {"gate", "secret_wall", "trapdoor"};
constexpr std::array<std::string_view, 3> kPlateWeights = {"any", "creature", "player"};
constexpr std::array<std::string_view, 4> kTeleportFilters = {"player", "creature", "items", "any"};
constexpr std::array<std::string_view, 3> kRelayModes = {"any", "all", "count"};

template <usize N>
std::vector<std::string> texts(const std::array<std::string_view, N>& names)
{
    return {names.begin(), names.end()};
}

/// What may receive events: moving parts, relays, mobs (doors, chests), spawn markers, teleporters.
bool receives(const world::Scene& scene, entt::entity e)
{
    const auto* t = scene.get<world::TriggerVolume>(e);
    return scene.has<world::Mover>(e) || scene.has<world::Relay>(e) || scene.has<world::MobRef>(e) ||
           scene.has<world::Spawn>(e) || (t != nullptr && t->teleport);
}

/// Targets and action of whatever sends from `e` (trigger, mob, relay), or nulls.
std::pair<std::vector<std::string>*, std::string*> senderOf(world::Scene& scene, entt::entity e)
{
    if (auto* t = scene.get<world::TriggerVolume>(e))
    {
        return {&t->targets, &t->action};
    }
    if (auto* r = scene.get<world::Relay>(e))
    {
        return {&r->targets, &r->action};
    }
    if (auto* m = scene.get<world::MobRef>(e))
    {
        return {&m->targets, &m->action};
    }
    return {nullptr, nullptr};
}
} // namespace

void Editor::dungeonFields(
    entt::entity e, const std::function<ui::EditorField&(std::string, ui::EditorField::Kind)>& addField)
{
    using Kind = ui::EditorField::Kind;
    world::Scene& scene = m_engine.scene();
    const auto choose = [&](std::string label, std::string_view current, std::span<const std::string> known)
    {
        ChoiceList list = choiceList(current, known);
        auto& field = addField(std::move(label), Kind::Choice);
        field.choices = std::move(list.items);
        field.choice = list.index;
    };
    const auto pick =
        [&](std::string label, std::string_view current, std::span<const std::string_view> names)
    {
        auto& field = addField(std::move(label), Kind::Choice);
        field.choices.assign(names.begin(), names.end());
        const auto at = std::ranges::find(names, current);
        field.choice = at != names.end() ? static_cast<i32>(at - names.begin()) : 0;
    };
    // The event net: targets by name (from a list of what can receive), the action.
    if (const auto [targets, action] = senderOf(scene, e); targets != nullptr)
    {
        addField("targets", Kind::Text).text = joinNames(*targets);
        std::vector<std::string> names;
        scene.each<world::Vob>(
            [&](entt::entity other, const world::Vob& vob)
            {
                if (other != e && !vob.id.runtime() && !vob.nameText.empty() && receives(scene, other) &&
                    std::ranges::find(*targets, vob.nameText) == targets->end())
                {
                    names.push_back(vob.nameText);
                }
            });
        std::ranges::sort(names);
        names.erase(std::unique(names.begin(), names.end()), names.end());
        choose("add target", {}, names);
        pick("action", *action, world::kEventActions);
        if (!targets->empty())
        {
            addField("probe: send", Kind::Button);
        }
    }
    if (const auto* t = scene.get<world::TriggerVolume>(e))
    {
        if (t->plate)
        {
            pick("plate weight", kPlateWeights[static_cast<usize>(t->plate->weight)], kPlateWeights);
            std::vector<std::string> actions = texts(world::kEventActions);
            choose("plate release", t->plate->releaseAction, actions);
        }
        if (t->teleport)
        {
            std::vector<std::string> places;
            scene.each<world::Vob>(
                [&](entt::entity other, const world::Vob& vob)
                {
                    const auto type = scene.get<world::StartPoint>(other);
                    if (other != e && !vob.id.runtime() && !vob.nameText.empty() &&
                        (type != nullptr || (!scene.has<world::MeshRef>(other) && !receives(scene, other) &&
                                             !scene.has<world::LightSource>(other))))
                    {
                        places.push_back(vob.nameText); // start points and markers (empties)
                    }
                });
            std::ranges::sort(places);
            choose("teleport to", t->teleport->to, places);
            choose("teleport world", t->teleport->world, m_worlds);
            const auto starts = startsOf(t->teleport->world).value_or(std::vector<std::string>{});
            choose("teleport start", t->teleport->start, starts);
            pick("teleport filter", kTeleportFilters[static_cast<usize>(t->teleport->filter)],
                 kTeleportFilters);
            addField("teleport visible", Kind::Flag).flag = t->teleport->visible;
            addField("teleport active", Kind::Flag).flag = t->teleport->active;
        }
        else if (!t->plate)
        {
            addField("make plate", Kind::Button);
            addField("make teleporter", Kind::Button);
        }
    }
    if (const auto* r = scene.get<world::Relay>(e))
    {
        pick("relay mode", kRelayModes[static_cast<usize>(r->mode)], kRelayModes);
        if (r->mode == world::Relay::Mode::Count)
        {
            addField("relay count", Kind::Number).number = static_cast<f32>(r->count);
        }
        addField("relay delay", Kind::Number).number = r->delay;
        addField("relay once", Kind::Flag).flag = r->once;
        addField("relay invert", Kind::Flag).flag = r->invert;
    }
    if (const auto* m = scene.get<world::MobRef>(e))
    {
        addField("switch once", Kind::Flag).flag = m->once;
        addField("hidden", Kind::Flag).flag = m->hidden;
        addField("no pick", Kind::Flag).flag = m->noPick;
        constexpr std::array<std::string_view, 3> kSides = {"-", "front", "back"};
        pick("one way", m->oneWay.empty() ? "-" : std::string_view(m->oneWay), kSides);
    }
    if (const auto* s = scene.get<world::Spawn>(e))
    {
        choose("spawn npc", s->npc, m_mobLists.npcs);
        addField("spawn count", Kind::Number).number = static_cast<f32>(s->count);
        addField("spawn respawn", Kind::Flag).flag = s->respawn;
        addField("spawn dormant", Kind::Flag).flag = s->dormant;
    }
    if (scene.has<world::MeshRef>(e))
    {
        const auto* mover = scene.get<world::Mover>(e);
        constexpr std::array<std::string_view, 4> kKinds = {"-", "gate", "secret_wall", "trapdoor"};
        pick("mover", mover != nullptr ? kMoverKinds[static_cast<usize>(mover->kind)] : "-", kKinds);
        if (mover != nullptr)
        {
            addField("mover open", Kind::Flag).flag = mover->open;
            addField("mover seconds", Kind::Number).number = mover->seconds;
            constexpr std::array<std::string_view, 2> kCrush = {"stop", "damage"};
            pick("mover crush", kCrush[static_cast<usize>(mover->crush)], kCrush);
            addField("mover sound move", Kind::Text).text = mover->soundMove;
            addField("mover sound stop", Kind::Text).text = mover->soundStop;
            addField("preview open", Kind::Button);
            addField("preview shut", Kind::Button);
        }
    }
}

bool Editor::applyDungeonField(entt::entity e, const ui::EditorField& f)
{
    world::Scene& scene = m_engine.scene();
    const std::string chosen = choiceValue(f.choices, f.choice);
    const auto index = [&](usize count)
    { return static_cast<usize>(std::clamp(f.choice, 0, i32(count) - 1)); };
    if (const auto [targets, action] = senderOf(scene, e); targets != nullptr)
    {
        if (f.label == "targets")
        {
            *targets = splitNames(f.text);
            return true;
        }
        if (f.label == "add target")
        {
            if (!chosen.empty())
            {
                targets->push_back(chosen);
            }
            return true;
        }
        if (f.label == "action")
        {
            *action = std::string(world::kEventActions[index(world::kEventActions.size())]);
            return true;
        }
    }
    if (auto* t = scene.get<world::TriggerVolume>(e))
    {
        if (t->plate && f.label == "plate weight")
        {
            t->plate->weight = static_cast<world::Plate::Weight>(index(kPlateWeights.size()));
            return true;
        }
        if (t->plate && f.label == "plate release")
        {
            t->plate->releaseAction = chosen;
            return true;
        }
        if (t->teleport && f.label.starts_with("teleport "))
        {
            world::Teleport& p = *t->teleport;
            if (f.label == "teleport to")
            {
                p.to = chosen;
                p.world.clear(); // one or the other (the format's rule)
                p.start.clear();
            }
            else if (f.label == "teleport world")
            {
                p.world = chosen;
                p.to.clear();
            }
            else if (f.label == "teleport start")
            {
                p.start = chosen;
            }
            else if (f.label == "teleport filter")
            {
                p.filter = static_cast<world::Teleport::Filter>(index(kTeleportFilters.size()));
            }
            p.visible = f.label == "teleport visible" ? f.flag : p.visible;
            p.active = f.label == "teleport active" ? f.flag : p.active;
            return true;
        }
    }
    if (auto* r = scene.get<world::Relay>(e); r && f.label.starts_with("relay "))
    {
        if (f.label == "relay mode")
        {
            r->mode = static_cast<world::Relay::Mode>(index(kRelayModes.size()));
        }
        r->count =
            f.label == "relay count" ? static_cast<u32>(std::clamp(f.number, 1.0f, 1'000'000.0f)) : r->count;
        r->delay = f.label == "relay delay" ? std::max(f.number, 0.0f) : r->delay;
        r->once = f.label == "relay once" ? f.flag : r->once;
        r->invert = f.label == "relay invert" ? f.flag : r->invert;
        return true;
    }
    if (auto* m = scene.get<world::MobRef>(e))
    {
        if (f.label == "switch once" || f.label == "hidden" || f.label == "no pick")
        {
            (f.label == "switch once" ? m->once : f.label == "hidden" ? m->hidden : m->noPick) = f.flag;
            return true;
        }
        if (f.label == "one way")
        {
            m->oneWay = f.choice == 1 ? "front" : f.choice == 2 ? "back" : "";
            return true;
        }
    }
    if (auto* s = scene.get<world::Spawn>(e); s && f.label.starts_with("spawn "))
    {
        if (f.label == "spawn npc" && !chosen.empty())
        {
            s->npc = chosen;
        }
        s->count = f.label == "spawn count" ? static_cast<u32>(std::clamp(f.number, 1.0f, 32.0f)) : s->count;
        s->respawn = f.label == "spawn respawn" ? f.flag : s->respawn;
        s->dormant = f.label == "spawn dormant" ? f.flag : s->dormant;
        return true;
    }
    if (scene.has<world::MeshRef>(e) && f.label.starts_with("mover"))
    {
        if (f.label == "mover")
        {
            if (f.choice <= 0)
            {
                scene.remove<world::Mover>(e);
            }
            else
            {
                world::Mover mover =
                    scene.has<world::Mover>(e) ? *scene.get<world::Mover>(e) : world::Mover{};
                const auto kind = static_cast<world::Mover::Kind>(std::min<usize>(f.choice - 1, 2));
                if (!scene.has<world::Mover>(e) || mover.kind != kind)
                {
                    mover.seconds = world::kMoverSeconds[static_cast<usize>(kind)];
                }
                mover.kind = kind;
                scene.set<world::Mover>(e, mover);
            }
            return true;
        }
        if (auto* mover = scene.get<world::Mover>(e))
        {
            mover->open = f.label == "mover open" ? f.flag : mover->open;
            mover->seconds = f.label == "mover seconds" ? std::max(f.number, 0.05f) : mover->seconds;
            if (f.label == "mover crush")
            {
                mover->crush = f.choice == 1 ? world::Mover::Crush::Damage : world::Mover::Crush::Stop;
            }
            mover->soundMove = f.label == "mover sound move" ? f.text : mover->soundMove;
            mover->soundStop = f.label == "mover sound stop" ? f.text : mover->soundStop;
        }
        return true;
    }
    return false;
}

void Editor::pressButton(const ui::EditorField& f)
{
    const world::VobId id = m_selection;
    if (f.label == "preview open" || f.label == "preview shut")
    {
        (void)previewMover(id, f.label == "preview open");
    }
    else if (f.label == "probe: send")
    {
        (void)probeSender(id);
    }
    else if (f.label == "make plate" || f.label == "make teleporter")
    {
        const bool plate = f.label == "make plate";
        (void)edit(std::format("{} {}", f.label, vobLabel(id)), {id},
                   [this, id, plate]() -> Result<void>
                   {
                       world::Scene& scene = m_engine.scene();
                       auto* t = scene.get<world::TriggerVolume>(scene.findById(id));
                       if (t == nullptr)
                       {
                           return Error{"no trigger"};
                       }
                       if (plate)
                       {
                           t->plate = world::Plate{};
                       }
                       else
                       {
                           t->teleport = world::Teleport{};
                       }
                       return {};
                   });
    }
}

bool Editor::previewMover(world::VobId id, std::optional<bool> open)
{
    const world::Scene& scene = m_engine.scene();
    const entt::entity e = scene.findById(id);
    const world::Vob* vob = scene.valid(e) ? scene.get<world::Vob>(e) : nullptr;
    if (vob == nullptr || !scene.has<world::Mover>(e) || !m_engine.previewMover(vob->nameText, open))
    {
        m_status = std::format("{}: no moving part", vobLabel(id));
        return false;
    }
    m_status = std::format("preview {}: {} (back before the next edit or save)", vobLabel(id),
                           m_engine.moverInfo(vob->nameText)->open ? "open" : "shut");
    return true;
}

usize Editor::probeSender(world::VobId id)
{
    world::Scene& scene = m_engine.scene();
    const entt::entity e = scene.findById(id);
    if (!scene.valid(e))
    {
        return 0;
    }
    const auto [targets, action] = senderOf(scene, e);
    if (targets == nullptr)
    {
        m_status = std::format("{} sends nothing", vobLabel(id));
        return 0;
    }
    usize reached = 0;
    usize others = 0;
    for (const std::string& target : *targets)
    {
        const auto info = m_engine.moverInfo(target);
        if (!info)
        {
            ++others;
            continue;
        }
        const std::string_view a = *action;
        const std::optional<bool> open = a == "open" || a == "on"          ? std::optional<bool>(true)
                                         : a == "close" || a == "off"      ? std::optional<bool>(false)
                                         : a == "toggle" || a == "trigger" ? std::optional<bool>(!info->open)
                                                                           : std::nullopt;
        if (open && m_engine.previewMover(target, open))
        {
            ++reached;
        }
    }
    m_status = std::format("probe {}: '{}' moved {} part(s){}", vobLabel(id), *action, reached,
                           others > 0 ? std::format(", {} other target(s): try them with F5", others) : "");
    return reached;
}

void Editor::drawDungeonLinks()
{
    render::DebugDraw& draw = m_engine.debugDraw();
    const Vec3 eye = m_engine.camera().transform.position;
    for (const DungeonLink& link : dungeonLinks(m_engine.scene()))
    {
        if (glm::length(link.fromPosition - eye) > kLinkDrawRadius)
        {
            continue;
        }
        const Vec4 colour = linkColor(link);
        if (!link.to.valid())
        {
            // A target that does not exist: a short red stub with its name.
            draw.line(link.fromPosition, link.fromPosition + Vec3(0.0f, 1.0f, 0.0f),
                      {{1.0f, 0.1f, 0.1f, 1.0f}});
            draw.text(link.fromPosition + Vec3(0.0f, 1.2f, 0.0f), "? " + link.target,
                      {{1.0f, 0.1f, 0.1f, 1.0f}});
            continue;
        }
        const bool selected = link.from == m_selection || link.to == m_selection;
        draw.arrow(link.fromPosition + kLinkLift, link.toPosition + kLinkLift,
                   {selected ? colour : Vec4(Vec3(colour) * 0.7f, 0.7f), 0.0f, !selected});
    }
}
} // namespace g7::editor
