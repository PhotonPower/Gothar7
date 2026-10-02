#include <g7/core/Profiler.hpp>

#include <algorithm>
#include <mutex>
#include <unordered_map>

namespace g7::profiler
{
namespace
{
/// Documented subsystem singleton: per-frame zone aggregation.
struct Collector
{
    std::mutex mutex;
    // Keyed by content, not pointer: the same literal may live at different addresses per TU.
    std::unordered_map<std::string_view, ZoneStats> current;
    std::vector<ZoneStats> last;
};

Collector& collector()
{
    static Collector instance;
    return instance;
}
} // namespace

ScopedZone::ScopedZone(const char* name) noexcept : m_name(name), m_start(std::chrono::steady_clock::now())
{
}

ScopedZone::~ScopedZone()
{
    const f64 ms = std::chrono::duration<f64, std::milli>(std::chrono::steady_clock::now() - m_start).count();
    Collector& c = collector();
    const std::scoped_lock lock(c.mutex);
    ZoneStats& stats = c.current[m_name];
    stats.name = m_name;
    ++stats.calls;
    stats.totalMs += ms;
    stats.maxMs = std::max(stats.maxMs, ms);
}

void markFrame()
{
    Collector& c = collector();
    const std::scoped_lock lock(c.mutex);
    c.last.clear();
    c.last.reserve(c.current.size());
    for (const auto& [name, stats] : c.current)
    {
        c.last.push_back(stats);
    }
    std::sort(c.last.begin(), c.last.end(),
              [](const ZoneStats& a, const ZoneStats& b) { return a.totalMs > b.totalMs; });
    c.current.clear();
}

std::vector<ZoneStats> lastFrame()
{
    Collector& c = collector();
    const std::scoped_lock lock(c.mutex);
    return c.last;
}
} // namespace g7::profiler
