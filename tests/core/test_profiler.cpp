#include <g7/core/Profiler.hpp>

#include <doctest/doctest.h>

#include <chrono>
#include <ostream> // doctest needs it to print std::string_view operands
#include <string>
#include <thread>
#include <vector>

using namespace g7;

namespace
{
const profiler::ZoneStats* findZone(const std::vector<profiler::ZoneStats>& zones, std::string_view name)
{
    for (const auto& zone : zones)
    {
        if (zone.name == name)
        {
            return &zone;
        }
    }
    return nullptr;
}

void busyWaitMs(f64 ms)
{
    const auto start = std::chrono::steady_clock::now();
    while (std::chrono::duration<f64, std::milli>(std::chrono::steady_clock::now() - start).count() < ms)
    {
    }
}

/// Discards whatever earlier tests collected.
void freshFrame()
{
    profiler::markFrame();
}
} // namespace

TEST_CASE("profiler: zone counts calls and time")
{
    freshFrame();
    for (int i = 0; i < 3; ++i)
    {
        const profiler::ScopedZone zone("test.work");
        busyWaitMs(i == 2 ? 2.0 : 0.1);
    }
    profiler::markFrame();

    const auto zones = profiler::lastFrame();
    const auto* work = findZone(zones, "test.work");
    REQUIRE(work != nullptr);
    CHECK(work->calls == 3);
    CHECK(work->maxMs >= 2.0);
    CHECK(work->totalMs >= work->maxMs);
}

TEST_CASE("profiler: nested zones are inclusive and sorted by total")
{
    freshFrame();
    {
        const profiler::ScopedZone outer("test.outer");
        busyWaitMs(0.5);
        {
            const profiler::ScopedZone inner("test.inner");
            busyWaitMs(1.0);
        }
    }
    profiler::markFrame();

    const auto zones = profiler::lastFrame();
    const auto* outer = findZone(zones, "test.outer");
    const auto* inner = findZone(zones, "test.inner");
    REQUIRE(outer != nullptr);
    REQUIRE(inner != nullptr);
    CHECK(outer->totalMs >= inner->totalMs);
    REQUIRE(zones.size() >= 2);
    for (usize i = 1; i < zones.size(); ++i)
    {
        CHECK(zones[i - 1].totalMs >= zones[i].totalMs);
    }
}

TEST_CASE("profiler: markFrame separates frames")
{
    freshFrame();
    {
        const profiler::ScopedZone zone("test.frame1");
    }
    profiler::markFrame();
    {
        const profiler::ScopedZone zone("test.frame2");
    }
    // frame2 is still being collected.
    auto zones = profiler::lastFrame();
    CHECK(findZone(zones, "test.frame1") != nullptr);
    CHECK(findZone(zones, "test.frame2") == nullptr);

    profiler::markFrame();
    zones = profiler::lastFrame();
    CHECK(findZone(zones, "test.frame1") == nullptr);
    CHECK(findZone(zones, "test.frame2") != nullptr);

    profiler::markFrame();
    CHECK(profiler::lastFrame().empty());
}

TEST_CASE("profiler: zones from several threads")
{
    freshFrame();
    std::vector<std::thread> threads;
    for (int t = 0; t < 4; ++t)
    {
        threads.emplace_back(
            []
            {
                for (int i = 0; i < 250; ++i)
                {
                    const profiler::ScopedZone zone("test.worker");
                }
            });
    }
    for (auto& thread : threads)
    {
        thread.join();
    }
    profiler::markFrame();

    const auto zones = profiler::lastFrame();
    const auto* worker = findZone(zones, "test.worker");
    REQUIRE(worker != nullptr);
    CHECK(worker->calls == 1000);
}

TEST_CASE("profiler: same name from different pointers is one zone")
{
    freshFrame();
    // Distinct arrays with static storage, so the two pointers differ.
    static const char a[] = "test.dynamic";
    static const char b[] = "test.dynamic";
    REQUIRE(static_cast<const void*>(a) != static_cast<const void*>(b));
    {
        const profiler::ScopedZone za(a);
    }
    {
        const profiler::ScopedZone zb(b);
    }
    profiler::markFrame();

    const auto zones = profiler::lastFrame();
    const auto* zone = findZone(zones, "test.dynamic");
    REQUIRE(zone != nullptr);
    CHECK(zone->calls == 2);
}

TEST_CASE("profiler: macros follow G7_PROFILING")
{
    freshFrame();
    {
        G7_PROFILE_SCOPE("test.macro");
        G7_PROFILE_FUNCTION();
    }
    G7_PROFILE_FRAME();
    // With profiling off the macros do nothing, so no frame was closed and no zone recorded.
    const bool recorded = findZone(profiler::lastFrame(), "test.macro") != nullptr;
    CHECK(recorded == (G7_PROFILING != 0));
}
