#include <g7/core/Log.hpp>
#include <g7/save/WorldDelta.hpp>

#include <algorithm>
#include <unordered_map>

namespace g7::save
{
WorldDelta diffWorld(const world::WorldFile& base, const world::WorldFile& now)
{
    WorldDelta delta;
    std::unordered_map<u64, const world::WorldFileVob*> before;
    for (const world::WorldFileVob& vob : base.vobs)
    {
        before.emplace(vob.id.value, &vob);
    }
    std::unordered_map<u64, bool> present;
    for (const world::WorldFileVob& vob : now.vobs)
    {
        present.emplace(vob.id.value, true);
        const auto it = before.find(vob.id.value);
        if (it == before.end() || world::writeVobJson(*it->second) != world::writeVobJson(vob))
        {
            delta.changed.push_back(vob);
        }
    }
    for (const world::WorldFileVob& vob : base.vobs)
    {
        if (!present.contains(vob.id.value))
        {
            delta.removed.push_back({vob.id, vob.name});
        }
    }
    std::sort(delta.changed.begin(), delta.changed.end(),
              [](const auto& a, const auto& b) { return a.id < b.id; });
    std::sort(delta.removed.begin(), delta.removed.end(),
              [](const auto& a, const auto& b) { return a.id < b.id; });
    delta.nextVobId = std::max(base.nextVobId, now.nextVobId);
    return delta;
}

world::WorldFile applyDelta(world::WorldFile base, const WorldDelta& delta, usize* dropped)
{
    usize skipped = 0;
    std::unordered_map<u64, usize> index;
    for (usize i = 0; i < base.vobs.size(); ++i)
    {
        index.emplace(base.vobs[i].id.value, i);
    }
    std::vector<bool> erase(base.vobs.size(), false);
    for (const RemovedVob& r : delta.removed)
    {
        const auto it = index.find(r.id.value);
        if (it == index.end())
        {
            continue; // gone from the file as well
        }
        if (base.vobs[it->second].name != r.name)
        {
            G7_LOG_WARN("save", "vob {}: '{}' in the save game, '{}' in the world file - not removed",
                        r.id.value, r.name, base.vobs[it->second].name);
            ++skipped;
            continue;
        }
        erase[it->second] = true;
    }
    for (const world::WorldFileVob& vob : delta.changed)
    {
        const auto it = index.find(vob.id.value);
        if (it == index.end())
        {
            if (vob.id.value < base.nextVobId && !vob.id.runtime())
            {
                // An id the file used once and dropped: not a vob made while playing.
                G7_LOG_WARN("save", "vob {} ('{}') no longer in the world file - left out", vob.id.value,
                            vob.name);
                ++skipped;
                continue;
            }
            base.vobs.push_back(vob);
            continue;
        }
        world::WorldFileVob& target = base.vobs[it->second];
        if (target.name != vob.name || target.type != vob.type)
        {
            G7_LOG_WARN("save", "vob {}: '{}' in the save game, '{}' in the world file - not changed",
                        vob.id.value, vob.name, target.name);
            ++skipped;
            continue;
        }
        target = vob;
    }
    usize w = 0;
    for (usize i = 0; i < base.vobs.size(); ++i)
    {
        if (i >= erase.size() || !erase[i])
        {
            if (w != i) // a self-move empties strings with libstdc++
            {
                base.vobs[w] = std::move(base.vobs[i]);
            }
            ++w;
        }
    }
    base.vobs.resize(w);
    // Children of removed vobs go with them (as Scene::destroyVob does).
    for (bool removedAny = true; removedAny;)
    {
        removedAny = false;
        std::unordered_map<u64, bool> ids;
        for (const world::WorldFileVob& vob : base.vobs)
        {
            ids.emplace(vob.id.value, true);
        }
        const auto orphan = [&](const world::WorldFileVob& vob)
        { return vob.parent.valid() && !ids.contains(vob.parent.value); };
        const auto before = base.vobs.size();
        std::erase_if(base.vobs, orphan);
        removedAny = base.vobs.size() != before;
    }
    base.nextVobId = std::max(base.nextVobId, delta.nextVobId);
    if (dropped != nullptr)
    {
        *dropped = skipped;
    }
    return base;
}
} // namespace g7::save
