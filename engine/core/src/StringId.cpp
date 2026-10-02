#include <g7/core/Assert.hpp>
#include <g7/core/Log.hpp>
#include <g7/core/StringId.hpp>

#if G7_STRINGID_NAMES
#include <mutex>
#include <string>
#include <unordered_map>
#endif

namespace g7
{
#if G7_STRINGID_NAMES
namespace
{
/// Documented subsystem singleton: hash -> first registered spelling.
struct NameRegistry
{
    std::mutex mutex;
    std::unordered_map<u64, std::string> names;
};

NameRegistry& registry()
{
    static NameRegistry instance;
    return instance;
}
} // namespace

StringId::StringId(std::string_view name) : m_hash(hashOf(name))
{
    NameRegistry& reg = registry();
    const std::scoped_lock lock(reg.mutex);
    const auto [it, inserted] = reg.names.try_emplace(m_hash, name);
    const bool collision = !inserted && !equalsIgnoreCase(it->second, name);
    if (collision)
    {
        G7_LOG_ERROR("core", "StringId collision: '{}' and '{}' share hash {:016x}", it->second, name,
                     m_hash);
    }
    G7_ASSERT(!collision, "StringId hash collision");
}

std::string_view StringId::name() const
{
    NameRegistry& reg = registry();
    const std::scoped_lock lock(reg.mutex);
    // Node-based map: element addresses stay valid across later insertions.
    const auto it = reg.names.find(m_hash);
    return it != reg.names.end() ? std::string_view(it->second) : std::string_view();
}
#else
StringId::StringId(std::string_view name) : m_hash(hashOf(name))
{
}

std::string_view StringId::name() const
{
    return {};
}
#endif
} // namespace g7
