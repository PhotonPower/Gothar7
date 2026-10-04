#pragma once

// Mobs (M8 part C, docs/modules/gameplay.md "Mob-Interaktion"): the mob types of assets/source/data/mobs.toml
// (contract engine - figuren - welt: slots, clips; characters-pipeline.md §3.1), choosing a user slot, and
// the lockpicking game of Gothic 1 (turn left/right along the lock's combination; a wrong turn resets it and
// may break the pick).

#include <g7/core/Math.hpp>
#include <g7/core/Result.hpp>
#include <g7/core/Types.hpp>

#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace g7::gameplay
{
struct MobSlot
{
    std::string name;
    Vec3 position{0.0f};            ///< feet of the user in mob space (Y up, the mob's front faces +Z)
    Vec3 facing{0.0f, 0.0f, -1.0f}; ///< where the user looks, horizontal unit vector in mob space
};

struct MobType
{
    std::string clips; ///< clip prefix, "mob/chest"
    std::string enter; ///< "t_open" (empty: none)
    std::string loop;  ///< "s_open"
    std::string leave; ///< "t_close"
    /// Further loops the mechanics choose ("picklock").
    std::map<std::string, std::string, std::less<>> extra;
    std::vector<MobSlot> slots;

    /// "mob/chest/t_open" for "t_open"; empty for an empty name.
    [[nodiscard]] std::string clip(std::string_view name) const;
};

struct MobTypes
{
    std::map<std::string, MobType, std::less<>> types;

    [[nodiscard]] const MobType* find(std::string_view type) const noexcept;
    /// mobs.toml v1. Errors name the file and the entry ("mobs.toml: mobs.chest.slots[0]: needs 'pos'").
    [[nodiscard]] static Result<MobTypes> parse(std::string_view toml, std::string_view source);
};

/// A slot placed in the world: where the user's feet go and the yaw it faces (0 = -Z, positive turns left,
/// as the player's movement).
struct SlotPlace
{
    usize index = 0;
    Vec3 feet{0.0f};
    f32 yaw = 0.0f;
};
[[nodiscard]] SlotPlace placeSlot(const MobType& type, usize index, const Mat4& mobWorld);
/// The slot closest to `from` whose bit is not set in `busy` (bit i = slot i is used); nullopt if all are
/// busy or the type has none.
[[nodiscard]] std::optional<SlotPlace> chooseSlot(const MobType& type, const Mat4& mobWorld, const Vec3& from,
                                                  u32 busy = 0);
/// The yaw (as the player's movement) of a horizontal direction.
[[nodiscard]] f32 yawOf(const Vec3& direction) noexcept;

/// Gothic 1 lockpicking. The combination is a string of 'L' and 'R'; each turn either fits the next step or
/// resets the lock to its start and, with `breakChance`, breaks the pick.
class Lockpick
{
public:
    enum class Result : u8
    {
        Progress, ///< the step fitted, more to come
        Opened,   ///< the last step fitted
        Reset,    ///< wrong: back to the start, the pick holds
        Broken,   ///< wrong: back to the start and the pick broke
    };

    explicit Lockpick(std::string combination);
    /// One turn ('L' or 'R'); `roll` in [0, 1) decides about breaking (roll < breakChance breaks).
    Result turn(char direction, f32 roll, f32 breakChance);
    [[nodiscard]] usize progress() const noexcept { return m_progress; }
    [[nodiscard]] usize length() const noexcept { return m_combination.size(); }
    [[nodiscard]] bool open() const noexcept { return m_progress == m_combination.size(); }
    /// "LRRL": only L and R, at least one.
    [[nodiscard]] static bool validCombination(std::string_view combination) noexcept;

private:
    std::string m_combination;
    usize m_progress = 0;
};
} // namespace g7::gameplay
