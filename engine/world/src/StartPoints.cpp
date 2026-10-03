#include <g7/core/StringUtil.hpp>
#include <g7/world/Scene.hpp>
#include <g7/world/StartPoints.hpp>

#include <algorithm>
#include <string>
#include <vector>

namespace g7::world
{
Result<entt::entity> findStartPoint(const Scene& scene, std::string_view name)
{
    std::vector<std::pair<VobId, entt::entity>> starts;
    scene.each<Vob, StartPoint>([&](entt::entity e, const Vob& vob, const StartPoint&)
                                { starts.emplace_back(vob.id, e); });
    if (starts.empty())
    {
        return Error{"the world has no start point"};
    }
    std::sort(starts.begin(), starts.end());
    if (name.empty())
    {
        return starts.front().second;
    }
    std::string known;
    for (const auto& [id, e] : starts)
    {
        const std::string& text = scene.get<Vob>(e)->nameText;
        if (equalsIgnoreCase(text, name))
        {
            return e;
        }
        known += (known.empty() ? "" : ", ") + text;
    }
    return Error{"unknown start point '" + std::string(name) + "' (the world has: " + known + ")"};
}
} // namespace g7::world
