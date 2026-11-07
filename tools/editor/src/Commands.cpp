#include <g7/editor/Commands.hpp>
#include <g7/world/Scene.hpp>
#include <g7/world/WorldFile.hpp>
#include <g7/world/WorldScene.hpp>

#include <algorithm>
#include <format>
#include <functional>
#include <unordered_map>

namespace g7::editor
{
std::vector<VobState> captureVobs(const world::Scene& scene, std::span<const world::VobId> ids)
{
    std::vector<VobState> out;
    std::vector<u64> seen;
    std::function<void(entt::entity)> add = [&](entt::entity e)
    {
        const world::VobId id = scene.idOf(e);
        if (std::ranges::find(seen, id.value) != seen.end())
        {
            return;
        }
        seen.push_back(id.value);
        out.push_back({id, world::writeVobJson(world::captureVob(scene, e))});
        std::vector<entt::entity> children = scene.children(e);
        std::sort(children.begin(), children.end(),
                  [&](entt::entity a, entt::entity b) { return scene.idOf(a) < scene.idOf(b); });
        for (const entt::entity child : children)
        {
            add(child);
        }
    };
    for (const world::VobId id : ids)
    {
        const entt::entity e = scene.findById(id);
        if (e == entt::null)
        {
            if (std::ranges::find(seen, id.value) == seen.end())
            {
                seen.push_back(id.value);
                out.push_back({id, std::nullopt});
            }
            continue;
        }
        add(e);
    }
    return out;
}

Result<void> restoreVobs(world::Scene& scene, std::span<const VobState> states)
{
    // First away what should not be there (a destroyed parent takes its children along).
    for (const VobState& s : states)
    {
        if (!s.json)
        {
            if (const entt::entity e = scene.findById(s.id); e != entt::null)
            {
                scene.destroyVob(e);
            }
        }
    }
    // Then the others, parents before children: parse all, then restore in rounds until each parent is there.
    std::vector<world::WorldFileVob> pending;
    for (const VobState& s : states)
    {
        if (s.json)
        {
            auto vob = world::parseVobJson(*s.json, "<undo>");
            if (!vob)
            {
                return vob.error();
            }
            pending.push_back(std::move(vob).value());
        }
    }
    while (!pending.empty())
    {
        const usize before = pending.size();
        std::erase_if(pending,
                      [&](const world::WorldFileVob& vob)
                      {
                          if (vob.parent.valid() && scene.findById(vob.parent) == entt::null)
                          {
                              return false; // its parent comes in a later round
                          }
                          return restoreVob(scene, vob).ok();
                      });
        if (pending.size() == before)
        {
            return Error{std::format("undo: cannot restore vob {} (parent {} missing)",
                                     pending.front().id.value, pending.front().parent.value)};
        }
    }
    scene.updateTransforms();
    return {};
}

void History::record(Command command)
{
    m_commands.resize(m_done); // what was undone cannot be redone after a new edit
    if (m_saved > static_cast<i64>(m_done))
    {
        m_saved = -1;
    }
    if (command.mergeable && !m_commands.empty())
    {
        Command& last = m_commands.back();
        const auto ids = [](const std::vector<VobState>& states)
        {
            std::vector<u64> v;
            for (const VobState& s : states)
            {
                v.push_back(s.id.value);
            }
            return v;
        };
        const auto names = [](const std::optional<WaynetSnapshot>& s)
        { return s ? s->names : std::vector<std::string>{}; };
        if (last.mergeable && last.label == command.label && ids(last.before) == ids(command.before) &&
            names(last.waynetBefore) == names(command.waynetBefore) && m_saved != static_cast<i64>(m_done))
        {
            last.after = std::move(command.after); // one step: from the first "before" to the last "after"
            last.waynetAfter = std::move(command.waynetAfter);
            return;
        }
    }
    m_commands.push_back(std::move(command));
    ++m_done;
    if (m_commands.size() > m_depth)
    {
        m_commands.erase(m_commands.begin());
        --m_done;
        m_saved = m_saved > 0 ? m_saved - 1 : -1;
    }
}

Result<bool> History::undo(world::Scene& scene, world::WaynetData* waynet)
{
    if (!canUndo())
    {
        return false;
    }
    const Command& c = m_commands[m_done - 1];
    if (auto restored = restoreVobs(scene, c.before); !restored)
    {
        return restored.error();
    }
    if (c.waynetBefore && waynet != nullptr)
    {
        if (auto restored = restoreWaynet(*waynet, *c.waynetBefore); !restored)
        {
            return restored.error();
        }
    }
    --m_done;
    return true;
}

Result<bool> History::redo(world::Scene& scene, world::WaynetData* waynet)
{
    if (!canRedo())
    {
        return false;
    }
    const Command& c = m_commands[m_done];
    if (auto restored = restoreVobs(scene, c.after); !restored)
    {
        return restored.error();
    }
    if (c.waynetAfter && waynet != nullptr)
    {
        if (auto restored = restoreWaynet(*waynet, *c.waynetAfter); !restored)
        {
            return restored.error();
        }
    }
    ++m_done;
    return true;
}

std::string History::undoLabel() const
{
    return canUndo() ? m_commands[m_done - 1].label : std::string();
}

std::string History::redoLabel() const
{
    return canRedo() ? m_commands[m_done].label : std::string();
}

void History::clear() noexcept
{
    m_commands.clear();
    m_done = 0;
    m_saved = 0;
}
} // namespace g7::editor
