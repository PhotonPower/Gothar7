#include <g7/core/Config.hpp>
#include <g7/core/Log.hpp>

// TOML_HEADER_ONLY=1 and TOML_EXCEPTIONS=0 come from the g7_tomlplusplus target (ADR 0010).
#include <toml++/toml.hpp>

#include <format>
#include <sstream>
#include <utility>

namespace g7
{
struct Config::Impl
{
    toml::table table;
};

namespace
{
std::vector<std::string_view> splitKey(std::string_view key)
{
    std::vector<std::string_view> parts;
    usize start = 0;
    while (true)
    {
        const usize dot = key.find('.', start);
        parts.push_back(
            key.substr(start, dot == std::string_view::npos ? std::string_view::npos : dot - start));
        if (dot == std::string_view::npos)
        {
            return parts;
        }
        start = dot + 1;
    }
}

const toml::node* lookup(const toml::table& table, std::string_view key)
{
    return key.empty() ? &table : table.at_path(key).node();
}

template <typename T>
std::optional<T> convert(const toml::node& node);

template <>
std::optional<bool> convert<bool>(const toml::node& node)
{
    if (const auto* v = node.as_boolean())
    {
        return v->get();
    }
    return std::nullopt;
}

template <>
std::optional<i64> convert<i64>(const toml::node& node)
{
    if (const auto* v = node.as_integer())
    {
        return v->get();
    }
    return std::nullopt;
}

template <>
std::optional<f64> convert<f64>(const toml::node& node)
{
    if (const auto* v = node.as_floating_point())
    {
        return v->get();
    }
    if (const auto* v = node.as_integer())
    {
        return static_cast<f64>(v->get());
    }
    return std::nullopt;
}

template <>
std::optional<std::string> convert<std::string>(const toml::node& node)
{
    if (const auto* v = node.as_string())
    {
        return v->get();
    }
    return std::nullopt;
}

template <>
std::optional<std::vector<std::string>> convert<std::vector<std::string>>(const toml::node& node)
{
    const auto* array = node.as_array();
    if (!array)
    {
        return std::nullopt;
    }
    std::vector<std::string> result;
    result.reserve(array->size());
    for (const toml::node& element : *array)
    {
        const auto* s = element.as_string();
        if (!s)
        {
            return std::nullopt;
        }
        result.push_back(s->get());
    }
    return result;
}

template <>
std::optional<std::vector<f64>> convert<std::vector<f64>>(const toml::node& node)
{
    const auto* array = node.as_array();
    if (!array)
    {
        return std::nullopt;
    }
    std::vector<f64> result;
    result.reserve(array->size());
    for (const toml::node& element : *array)
    {
        auto value = convert<f64>(element);
        if (!value)
        {
            return std::nullopt;
        }
        result.push_back(*value);
    }
    return result;
}

void insertValue(toml::table& table, std::string_view key, bool value)
{
    table.insert_or_assign(key, value);
}
void insertValue(toml::table& table, std::string_view key, i64 value)
{
    table.insert_or_assign(key, value);
}
void insertValue(toml::table& table, std::string_view key, f64 value)
{
    table.insert_or_assign(key, value);
}
void insertValue(toml::table& table, std::string_view key, std::string value)
{
    table.insert_or_assign(key, std::move(value));
}
void insertValue(toml::table& table, std::string_view key, std::vector<std::string> value)
{
    toml::array array;
    for (auto& s : value)
    {
        array.push_back(std::move(s));
    }
    table.insert_or_assign(key, std::move(array));
}

void insertValue(toml::table& table, std::string_view key, std::vector<f64> value)
{
    toml::array array;
    for (const f64 v : value)
    {
        array.push_back(v);
    }
    table.insert_or_assign(key, std::move(array));
}

void mergeTables(toml::table& target, const toml::table& source)
{
    for (const auto& [key, node] : source)
    {
        if (const auto* sourceTable = node.as_table())
        {
            if (auto* targetTable = target.get_as<toml::table>(key.str()))
            {
                mergeTables(*targetTable, *sourceTable);
                continue;
            }
        }
        node.visit([&](const auto& value) { target.insert_or_assign(key, value); });
    }
}
} // namespace

Config::Config() : m_impl(std::make_unique<Impl>())
{
}
Config::~Config() = default;
Config::Config(const Config& other) : m_impl(std::make_unique<Impl>(*other.m_impl))
{
}
Config& Config::operator=(const Config& other)
{
    if (this != &other)
    {
        *m_impl = *other.m_impl;
    }
    return *this;
}
Config::Config(Config&& other) noexcept = default;
Config& Config::operator=(Config&& other) noexcept = default;

Result<Config> Config::parse(std::string_view toml, std::string_view sourceName)
{
    toml::parse_result parsed = toml::parse(toml, sourceName);
    if (!parsed)
    {
        const toml::parse_error& error = parsed.error();
        return Error{std::format("{}:{}:{}: {}", sourceName, error.source().begin.line,
                                 error.source().begin.column, error.description())};
    }
    Config config;
    config.m_impl->table = std::move(parsed).table();
    return config;
}

Result<Config> Config::load(const fs::Path& path)
{
    auto text = fs::readText(path);
    if (!text)
    {
        return text.error();
    }
    return parse(text.value(), fs::toUtf8(path));
}

Result<void> Config::save(const fs::Path& path) const
{
    return fs::writeTextAtomic(path, toToml());
}

std::string Config::toToml() const
{
    std::ostringstream out;
    out << m_impl->table;
    out << '\n';
    return out.str();
}

bool Config::contains(std::string_view key) const
{
    return lookup(m_impl->table, key) != nullptr;
}

template <ConfigValue T>
std::optional<T> Config::find(std::string_view key) const
{
    const toml::node* node = lookup(m_impl->table, key);
    return node ? convert<T>(*node) : std::nullopt;
}

template <ConfigValue T>
T Config::get(std::string_view key, std::type_identity_t<T> defaultValue) const
{
    const toml::node* node = lookup(m_impl->table, key);
    if (!node)
    {
        return defaultValue;
    }
    if (auto value = convert<T>(*node))
    {
        return std::move(*value);
    }
    G7_LOG_WARN("core", "config key '{}' has unexpected type, using default", key);
    return defaultValue;
}

template <ConfigValue T>
void Config::set(std::string_view key, std::type_identity_t<T> value)
{
    G7_ASSERT(!key.empty(), "Config::set needs a key");
    const std::vector<std::string_view> parts = splitKey(key);
    toml::table* table = &m_impl->table;
    for (usize i = 0; i + 1 < parts.size(); ++i)
    {
        auto* child = table->get_as<toml::table>(parts[i]);
        if (!child)
        {
            // Replaces a non-table value of the same name: the new key wins.
            child = table->insert_or_assign(parts[i], toml::table{}).first->second.as_table();
        }
        table = child;
    }
    insertValue(*table, parts.back(), std::move(value));
}

usize Config::arraySize(std::string_view key) const
{
    const toml::node* node = lookup(m_impl->table, key);
    const auto* array = node ? node->as_array() : nullptr;
    return array ? array->size() : 0;
}

std::vector<std::string> Config::keys(std::string_view table) const
{
    std::vector<std::string> result;
    const toml::node* node = lookup(m_impl->table, table);
    if (const auto* t = node ? node->as_table() : nullptr)
    {
        result.reserve(t->size());
        for (const auto& [key, value] : *t)
        {
            result.emplace_back(key.str());
        }
    }
    return result;
}

void Config::merge(const Config& overrides)
{
    mergeTables(m_impl->table, overrides.m_impl->table);
}

#define G7_CONFIG_INSTANTIATE(T)                                                                             \
    template std::optional<T> Config::find<T>(std::string_view) const;                                       \
    template T Config::get<T>(std::string_view, std::type_identity_t<T>) const;                              \
    template void Config::set<T>(std::string_view, std::type_identity_t<T>);

G7_CONFIG_INSTANTIATE(bool)
G7_CONFIG_INSTANTIATE(i64)
G7_CONFIG_INSTANTIATE(f64)
G7_CONFIG_INSTANTIATE(std::string)
G7_CONFIG_INSTANTIATE(std::vector<std::string>)
G7_CONFIG_INSTANTIATE(std::vector<f64>)

#undef G7_CONFIG_INSTANTIATE
} // namespace g7
