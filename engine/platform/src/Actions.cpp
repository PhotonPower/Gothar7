#include <g7/core/Log.hpp>
#include <g7/core/StringUtil.hpp>
#include <g7/platform/Actions.hpp>

#include <algorithm>
#include <string>

namespace g7::platform
{
namespace
{
constexpr std::array<std::string_view, static_cast<usize>(Action::Count)> kActionNames = {
    "move_forward", "move_back",  "strafe_left", "strafe_right", "turn_left", "turn_right", "walk",
    "sneak",        "jump",       "action",      "attack",       "use",       "parry",      "draw_weapon",
    "draw_ranged",  "draw_magic", "inventory",   "log",          "status",    "map",        "quick_save",
    "quick_load",   "console",    "pause",       "debug_draw",   "debug_ui",  "debug_fly",  "fly_forward",
    "fly_back",     "fly_left",   "fly_right",   "fly_up",       "fly_down",  "fly_fast",   "copy_position",
};

std::string tablePath(std::string_view scheme)
{
    return "bindings." + std::string(scheme);
}

bool isDown(const Input& input, const InputBinding& binding) noexcept
{
    return std::visit([&](auto value) { return input.isDown(value); }, binding);
}
bool pressed(const Input& input, const InputBinding& binding) noexcept
{
    return std::visit([&](auto value) { return input.pressed(value); }, binding);
}
bool released(const Input& input, const InputBinding& binding) noexcept
{
    return std::visit([&](auto value) { return input.released(value); }, binding);
}
} // namespace

std::string_view name(Action action) noexcept
{
    const auto index = static_cast<usize>(action);
    return index < kActionNames.size() ? kActionNames[index] : std::string_view("unknown");
}

std::optional<Action> actionFromName(std::string_view actionName) noexcept
{
    for (usize i = 0; i < kActionNames.size(); ++i)
    {
        if (equalsIgnoreCase(kActionNames[i], actionName))
        {
            return static_cast<Action>(i);
        }
    }
    return std::nullopt;
}

std::optional<InputBinding> bindingFromName(std::string_view bindingName) noexcept
{
    if (const auto key = keyFromName(bindingName))
    {
        return InputBinding(*key);
    }
    if (const auto button = mouseButtonFromName(bindingName))
    {
        return InputBinding(*button);
    }
    if (const auto button = gamepadButtonFromName(bindingName))
    {
        return InputBinding(*button);
    }
    return std::nullopt;
}

std::string_view name(const InputBinding& binding) noexcept
{
    return std::visit([](auto value) { return name(value); }, binding);
}

ActionMap ActionMap::fromConfig(const Config& config, std::string_view scheme)
{
    ActionMap map;
    const std::string table = tablePath(scheme);
    if (!config.contains(table))
    {
        G7_LOG_WARN("platform", "no input bindings for scheme '{}' ([{}] missing)", scheme, table);
        return map;
    }
    for (const std::string& actionName : config.keys(table))
    {
        const auto action = actionFromName(actionName);
        if (!action)
        {
            G7_LOG_WARN("platform", "[{}]: unknown action '{}' ignored", table, actionName);
            continue;
        }
        const auto inputs = config.find<std::vector<std::string>>(table + "." + actionName);
        if (!inputs)
        {
            G7_LOG_WARN("platform", "[{}]: '{}' must be a list of input names", table, actionName);
            continue;
        }
        for (const std::string& inputName : *inputs)
        {
            if (const auto binding = bindingFromName(inputName))
            {
                map.bind(*action, *binding);
            }
            else
            {
                G7_LOG_WARN("platform", "[{}]: unknown input '{}' for '{}' ignored", table, inputName,
                            actionName);
            }
        }
    }
    return map;
}

void ActionMap::writeTo(Config& config, std::string_view scheme) const
{
    const std::string table = tablePath(scheme);
    for (usize i = 0; i < m_bindings.size(); ++i)
    {
        std::vector<std::string> names;
        names.reserve(m_bindings[i].size());
        for (const InputBinding& binding : m_bindings[i])
        {
            names.emplace_back(name(binding));
        }
        config.set<std::vector<std::string>>(table + "." + std::string(kActionNames[i]), std::move(names));
    }
}

void ActionMap::bind(Action action, InputBinding binding)
{
    const auto index = static_cast<usize>(action);
    if (index >= m_bindings.size())
    {
        return;
    }
    auto& list = m_bindings[index];
    if (std::find(list.begin(), list.end(), binding) == list.end())
    {
        list.push_back(binding);
    }
}

void ActionMap::clear(Action action)
{
    const auto index = static_cast<usize>(action);
    if (index < m_bindings.size())
    {
        m_bindings[index].clear();
    }
}

std::span<const InputBinding> ActionMap::bindings(Action action) const noexcept
{
    const auto index = static_cast<usize>(action);
    return index < m_bindings.size() ? std::span<const InputBinding>(m_bindings[index])
                                     : std::span<const InputBinding>();
}

bool ActionMap::isDown(const Input& input, Action action) const noexcept
{
    const auto list = bindings(action);
    return std::any_of(list.begin(), list.end(),
                       [&](const InputBinding& b) { return platform::isDown(input, b); });
}

bool ActionMap::pressed(const Input& input, Action action) const noexcept
{
    const auto list = bindings(action);
    return std::any_of(list.begin(), list.end(),
                       [&](const InputBinding& b) { return platform::pressed(input, b); });
}

bool ActionMap::released(const Input& input, Action action) const noexcept
{
    const auto list = bindings(action);
    const bool anyReleased = std::any_of(list.begin(), list.end(),
                                         [&](const InputBinding& b) { return platform::released(input, b); });
    return anyReleased && !isDown(input, action);
}
} // namespace g7::platform
