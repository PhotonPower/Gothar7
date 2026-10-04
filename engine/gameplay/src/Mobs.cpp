#include <g7/core/Config.hpp>
#include <g7/gameplay/Mobs.hpp>

#include <algorithm>
#include <cmath>
#include <format>

namespace g7::gameplay
{
std::string MobType::clip(std::string_view name) const
{
    return name.empty() ? std::string() : std::format("{}/{}", clips, name);
}

const MobType* MobTypes::find(std::string_view type) const noexcept
{
    const auto it = types.find(type);
    return it == types.end() ? nullptr : &it->second;
}

Result<MobTypes> MobTypes::parse(std::string_view toml, std::string_view source)
{
    auto parsed = Config::parse(toml, source);
    if (!parsed)
    {
        return parsed.error();
    }
    const Config& c = parsed.value();
    if (c.get<i64>("version", 0) != 1)
    {
        return Error{std::format("{}: needs 'version = 1'", source)};
    }
    MobTypes out;
    for (const std::string& name : c.keys("mobs"))
    {
        const std::string at = "mobs." + name;
        const auto fail = [&](const std::string& where, std::string_view what)
        { return Error{std::format("{}: {}: {}", source, where, what)}; };
        MobType type;
        type.clips = c.get<std::string>(at + ".clips", "mob/" + name);
        type.enter = c.get<std::string>(at + ".enter", "");
        type.loop = c.get<std::string>(at + ".loop", "");
        type.leave = c.get<std::string>(at + ".leave", "");
        for (const std::string& key : c.keys(at + ".extra"))
        {
            const auto clip = c.find<std::string>(at + ".extra." + key);
            if (!clip)
            {
                return fail(at + ".extra." + key, "must be a clip name");
            }
            type.extra[key] = *clip;
        }
        const usize slots = c.arraySize(at + ".slots");
        for (usize i = 0; i < slots; ++i)
        {
            const std::string where = std::format("{}.slots[{}]", at, i);
            MobSlot slot;
            slot.name = c.get<std::string>(where + ".name", std::format("slot{}", i));
            const auto vec3 = [&](const char* key, Vec3& value) -> Result<void>
            {
                const std::string k = std::format("{}.{}", where, key);
                if (c.arraySize(k) != 3)
                {
                    return fail(where, std::format("needs '{}' as three numbers", key));
                }
                for (int a = 0; a < 3; ++a)
                {
                    const auto n = c.find<f64>(std::format("{}[{}]", k, a));
                    if (!n)
                    {
                        return fail(where, std::format("needs '{}' as three numbers", key));
                    }
                    value[a] = static_cast<f32>(*n);
                }
                return {};
            };
            if (auto ok = vec3("pos", slot.position); !ok)
            {
                return ok.error();
            }
            if (auto ok = vec3("facing", slot.facing); !ok)
            {
                return ok.error();
            }
            slot.facing.y = 0.0f;
            if (glm::length(slot.facing) < 1e-4f)
            {
                return fail(where, "'facing' must point somewhere horizontally");
            }
            slot.facing = glm::normalize(slot.facing);
            type.slots.push_back(std::move(slot));
        }
        out.types.emplace(name, std::move(type));
    }
    return out;
}

f32 yawOf(const Vec3& direction) noexcept
{
    // forwardOf(yaw) = (-sin yaw, 0, -cos yaw)
    return std::atan2(-direction.x, -direction.z);
}

SlotPlace placeSlot(const MobType& type, usize index, const Mat4& mobWorld)
{
    const MobSlot& slot = type.slots[index];
    const Vec3 feet = Vec3(mobWorld * Vec4(slot.position, 1.0f));
    Vec3 facing = Vec3(mobWorld * Vec4(slot.facing, 0.0f));
    facing.y = 0.0f;
    return SlotPlace{index, feet,
                     yawOf(glm::length(facing) > 1e-6f ? glm::normalize(facing) : Vec3(0, 0, -1))};
}

std::optional<SlotPlace> chooseSlot(const MobType& type, const Mat4& mobWorld, const Vec3& from, u32 busy)
{
    std::optional<SlotPlace> best;
    f32 bestDistance = 0.0f;
    for (usize i = 0; i < type.slots.size() && i < 32; ++i)
    {
        if ((busy >> i) & 1u)
        {
            continue;
        }
        const SlotPlace place = placeSlot(type, i, mobWorld);
        const f32 distance = glm::length(place.feet - from);
        if (!best || distance < bestDistance)
        {
            best = place;
            bestDistance = distance;
        }
    }
    return best;
}

Lockpick::Lockpick(std::string combination) : m_combination(std::move(combination))
{
}

bool Lockpick::validCombination(std::string_view combination) noexcept
{
    return !combination.empty() &&
           std::all_of(combination.begin(), combination.end(), [](char c) { return c == 'L' || c == 'R'; });
}

Lockpick::Result Lockpick::turn(char direction, f32 roll, f32 breakChance)
{
    if (open())
    {
        return Result::Opened;
    }
    if (m_combination[m_progress] == direction)
    {
        ++m_progress;
        return open() ? Result::Opened : Result::Progress;
    }
    m_progress = 0;
    return roll < breakChance ? Result::Broken : Result::Reset;
}
} // namespace g7::gameplay
