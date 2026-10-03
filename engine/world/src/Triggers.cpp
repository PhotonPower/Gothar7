#include <g7/world/Scene.hpp>
#include <g7/world/Triggers.hpp>

#include <algorithm>

namespace g7::world
{
bool TriggerSystem::contains(const TriggerVolume& volume, const Mat4& world, const Vec3& point) noexcept
{
    if (volume.shape == TriggerVolume::Shape::Sphere)
    {
        const f32 scale =
            std::max({glm::length(Vec3(world[0])), glm::length(Vec3(world[1])), glm::length(Vec3(world[2]))});
        return glm::length(point - Vec3(world[3])) <= volume.radius * scale;
    }
    const Vec3 local = Vec3(glm::inverse(world) * Vec4(point, 1.0f));
    return glm::all(glm::lessThanEqual(glm::abs(local), volume.halfExtents));
}

std::vector<TriggerEvent> TriggerSystem::update(const Scene& scene, std::span<const TriggerProbe> probes)
{
    std::vector<TriggerEvent> events;
    std::set<std::pair<u64, u64>> inside;
    std::set<u64> spent; // once-triggers entered now: every probe entering in this update counts
    // Function names point into the scene's components; collected first, reported after sorting.
    scene.each<Vob, TriggerVolume, WorldTransform>(
        [&](entt::entity, const Vob& vob, const TriggerVolume& volume, const WorldTransform& transform)
        {
            for (const TriggerProbe& probe : probes)
            {
                const bool wanted = volume.filter == TriggerVolume::Filter::Any ||
                                    (volume.filter == TriggerVolume::Filter::Player) == probe.player;
                if (!wanted || !contains(volume, transform.matrix, probe.position))
                {
                    continue;
                }
                const std::pair key{vob.id.value, probe.who.value};
                if (m_inside.contains(key))
                {
                    inside.insert(key); // still inside
                }
                else if (!volume.once || !m_spent.contains(vob.id.value))
                {
                    inside.insert(key);
                    spent.insert(vob.id.value);
                    events.push_back({TriggerEvent::Kind::Enter, vob.id, probe.who, volume.onEnter});
                }
            }
        });
    for (const auto& [trigger, who] : m_inside)
    {
        if (!inside.contains({trigger, who}))
        {
            const entt::entity e = scene.findById(VobId{trigger});
            const TriggerVolume* volume = e != entt::null ? scene.get<TriggerVolume>(e) : nullptr;
            events.push_back({TriggerEvent::Kind::Leave, VobId{trigger}, VobId{who},
                              volume != nullptr ? std::string_view(volume->onLeave) : std::string_view()});
        }
    }
    m_inside = std::move(inside);
    m_spent.insert(spent.begin(), spent.end());
    std::sort(events.begin(), events.end(), [](const TriggerEvent& a, const TriggerEvent& b)
              { return std::tie(a.trigger, a.who, a.kind) < std::tie(b.trigger, b.who, b.kind); });
    if (m_callback)
    {
        for (const TriggerEvent& event : events)
        {
            m_callback(event);
        }
    }
    return events;
}

void TriggerSystem::prime(const Scene& scene, std::span<const TriggerProbe> probes)
{
    m_inside.clear();
    scene.each<Vob, TriggerVolume, WorldTransform>(
        [&](entt::entity, const Vob& vob, const TriggerVolume& volume, const WorldTransform& transform)
        {
            for (const TriggerProbe& probe : probes)
            {
                const bool wanted = volume.filter == TriggerVolume::Filter::Any ||
                                    (volume.filter == TriggerVolume::Filter::Player) == probe.player;
                if (wanted && contains(volume, transform.matrix, probe.position))
                {
                    m_inside.insert({vob.id.value, probe.who.value});
                }
            }
        });
}

bool TriggerSystem::isInside(VobId trigger, VobId who) const
{
    return m_inside.contains({trigger.value, who.value});
}

void TriggerSystem::reset()
{
    m_inside.clear();
    m_spent.clear();
}
} // namespace g7::world
