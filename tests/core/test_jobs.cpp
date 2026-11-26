#include <g7/core/Jobs.hpp>

#include <doctest/doctest.h>

#include <algorithm>
#include <atomic>
#include <mutex>
#include <numeric>
#include <set>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

using namespace g7;

TEST_CASE("Jobs: thread counts")
{
    const u32 automatic = JobSystem::autoThreads();
    CHECK(automatic >= 1);
    CHECK(automatic <= 8);
    CHECK(automatic <= std::max(1u, std::thread::hardware_concurrency()));

    JobSystem autoJobs(0);
    CHECK(autoJobs.threads() == automatic);
    CHECK(autoJobs.workers() == automatic - 1);

    JobSystem serial(1);
    CHECK(serial.threads() == 1);
    CHECK(serial.workers() == 0);

    JobSystem four(4);
    CHECK(four.threads() == 4);
    CHECK(four.workers() == 3);
}

TEST_CASE("Jobs: parallelFor visits every index exactly once")
{
    for (const u32 threads : {1u, 2u, 4u, 8u})
    {
        CAPTURE(threads);
        JobSystem jobs(threads);
        for (const usize count : {usize{0}, usize{1}, usize{7}, usize{64}, usize{1000}, usize{4099}})
        {
            for (const usize chunk : {usize{0}, usize{1}, usize{3}, usize{64}, usize{5000}})
            {
                CAPTURE(count);
                CAPTURE(chunk);
                std::vector<std::atomic<u32>> visits(count);
                jobs.parallelFor(count, chunk,
                                 [&](usize begin, usize end)
                                 {
                                     for (usize i = begin; i < end; ++i)
                                     {
                                         visits[i].fetch_add(1);
                                     }
                                 });
                CHECK(std::all_of(visits.begin(), visits.end(), [](const auto& v) { return v.load() == 1; }));
            }
        }
    }
}

TEST_CASE("Jobs: chunk boundaries")
{
    JobSystem jobs(4);
    std::mutex mutex;
    std::vector<std::pair<usize, usize>> ranges;
    jobs.parallelFor(10, 3,
                     [&](usize begin, usize end)
                     {
                         const std::lock_guard lock(mutex);
                         ranges.emplace_back(begin, end);
                     });
    std::sort(ranges.begin(), ranges.end());
    const std::vector<std::pair<usize, usize>> expected = {{0, 3}, {3, 6}, {6, 9}, {9, 10}};
    CHECK(ranges == expected);

    // Automatic chunks: about four per thread, never empty, covering everything.
    ranges.clear();
    jobs.parallelFor(1000, 0,
                     [&](usize begin, usize end)
                     {
                         const std::lock_guard lock(mutex);
                         ranges.emplace_back(begin, end);
                     });
    std::sort(ranges.begin(), ranges.end());
    REQUIRE(!ranges.empty());
    CHECK(ranges.size() == 16);
    CHECK(ranges.front().first == 0);
    CHECK(ranges.back().second == 1000);
    for (usize i = 0; i < ranges.size(); ++i)
    {
        CHECK(ranges[i].first < ranges[i].second);
        if (i > 0)
        {
            CHECK(ranges[i].first == ranges[i - 1].second);
        }
    }
}

TEST_CASE("Jobs: results in their own slots come out in index order")
{
    JobSystem jobs(4);
    constexpr usize kCount = 10000;
    std::vector<u64> results(kCount, 0);
    jobs.parallelFor(kCount, 37,
                     [&](usize begin, usize end)
                     {
                         for (usize i = begin; i < end; ++i)
                         {
                             results[i] = static_cast<u64>(i) * static_cast<u64>(i);
                         }
                     });
    for (usize i = 0; i < kCount; ++i)
    {
        REQUIRE(results[i] == static_cast<u64>(i) * static_cast<u64>(i));
    }
    // The same as serially, whatever the thread count.
    JobSystem serial(1);
    std::vector<u64> serialResults(kCount, 0);
    serial.parallelFor(kCount, 37,
                       [&](usize begin, usize end)
                       {
                           for (usize i = begin; i < end; ++i)
                           {
                               serialResults[i] = static_cast<u64>(i) * static_cast<u64>(i);
                           }
                       });
    CHECK(results == serialResults);
}

TEST_CASE("Jobs: one thread runs everything serially on the caller, chunks in order")
{
    JobSystem jobs(1);
    const auto caller = std::this_thread::get_id();
    std::vector<usize> begins;
    bool otherThread = false;
    jobs.parallelFor(20, 6,
                     [&](usize begin, usize /*end*/)
                     {
                         begins.push_back(begin);
                         otherThread = otherThread || std::this_thread::get_id() != caller;
                         CHECK(JobSystem::insideJob());
                     });
    CHECK_FALSE(otherThread);
    CHECK(begins == std::vector<usize>{0, 6, 12, 18});
    CHECK_FALSE(JobSystem::insideJob());

    JobGroup group(jobs);
    bool ran = false;
    std::thread::id runner;
    group.run(
        [&]
        {
            ran = true;
            runner = std::this_thread::get_id();
        });
    CHECK(ran); // at once, there are no workers
    group.wait();
    CHECK(runner == caller);
}

TEST_CASE("Jobs: the caller works along")
{
    JobSystem jobs(4);
    const auto caller = std::this_thread::get_id();
    std::atomic<u32> byCaller{0};
    std::mutex mutex;
    std::set<std::thread::id> threads;
    jobs.parallelFor(400, 1,
                     [&](usize, usize)
                     {
                         if (std::this_thread::get_id() == caller)
                         {
                             byCaller.fetch_add(1);
                         }
                         const std::lock_guard lock(mutex);
                         threads.insert(std::this_thread::get_id());
                         std::this_thread::yield();
                     });
    CHECK(byCaller.load() > 0);
    CHECK(threads.size() <= 4);
}

TEST_CASE("Jobs: nested parallelFor runs serially on the job's thread")
{
    JobSystem jobs(4);
    std::vector<std::atomic<u32>> visits(16 * 50);
    std::atomic<bool> nestedOnOtherThread{false};
    jobs.parallelFor(16, 1,
                     [&](usize outer, usize)
                     {
                         CHECK(JobSystem::insideJob());
                         const auto thread = std::this_thread::get_id();
                         jobs.parallelFor(50, 7,
                                          [&](usize begin, usize end)
                                          {
                                              if (std::this_thread::get_id() != thread)
                                              {
                                                  nestedOnOtherThread = true;
                                              }
                                              for (usize i = begin; i < end; ++i)
                                              {
                                                  visits[outer * 50 + i].fetch_add(1);
                                              }
                                          });
                     });
    CHECK_FALSE(nestedOnOtherThread.load());
    CHECK(std::all_of(visits.begin(), visits.end(), [](const auto& v) { return v.load() == 1; }));
}

TEST_CASE("Jobs: an exception is rethrown on the calling thread")
{
    for (const u32 threads : {1u, 4u})
    {
        CAPTURE(threads);
        JobSystem jobs(threads);
        std::atomic<u32> chunksRun{0};
        CHECK_THROWS_WITH_AS(jobs.parallelFor(100, 1,
                                              [&](usize begin, usize)
                                              {
                                                  chunksRun.fetch_add(1);
                                                  if (begin == 42)
                                                  {
                                                      throw std::runtime_error("chunk 42");
                                                  }
                                              }),
                             "chunk 42", std::runtime_error);
        CHECK(chunksRun.load() >= 1);
        CHECK(chunksRun.load() <= 100);

        // The system stays usable.
        std::atomic<usize> sum{0};
        jobs.parallelFor(100, 10,
                         [&](usize begin, usize end)
                         {
                             for (usize i = begin; i < end; ++i)
                             {
                                 sum.fetch_add(i);
                             }
                         });
        CHECK(sum.load() == 4950);

        // Groups: the first exception at wait(), the other jobs still run.
        JobGroup group(jobs);
        std::atomic<u32> ran{0};
        for (u32 i = 0; i < 20; ++i)
        {
            group.run(
                [&ran, i]
                {
                    ran.fetch_add(1);
                    if (i == 5)
                    {
                        throw std::runtime_error("job 5");
                    }
                });
        }
        CHECK_THROWS_WITH_AS(group.wait(), "job 5", std::runtime_error);
        CHECK(ran.load() == 20);
        CHECK_NOTHROW(group.wait()); // reported once
    }
}

TEST_CASE("Jobs: groups run every job and can be reused")
{
    JobSystem jobs(4);
    std::vector<u32> slots(64, 0);
    JobGroup group(jobs);
    for (u32 round = 1; round <= 3; ++round)
    {
        for (usize i = 0; i < slots.size(); ++i)
        {
            group.run([&slots, i, round] { slots[i] += round; });
        }
        group.wait();
    }
    CHECK(std::all_of(slots.begin(), slots.end(), [](u32 v) { return v == 6; }));

    // A parallelFor inside a group job is nested: serial.
    std::atomic<usize> sum{0};
    for (u32 i = 0; i < 8; ++i)
    {
        group.run(
            [&]
            {
                jobs.parallelFor(10, 2,
                                 [&](usize begin, usize end)
                                 {
                                     for (usize j = begin; j < end; ++j)
                                     {
                                         sum.fetch_add(j);
                                     }
                                 });
            });
    }
    group.wait();
    CHECK(sum.load() == 8 * 45);
}

TEST_CASE("Jobs: stress")
{
    JobSystem jobs(8);
    u64 total = 0;
    for (u32 round = 0; round < 2000; ++round)
    {
        const usize count = 1 + (round * 7919u) % 300;
        std::vector<u64> results(count, 0);
        jobs.parallelFor(count, 1 + round % 9,
                         [&](usize begin, usize end)
                         {
                             for (usize i = begin; i < end; ++i)
                             {
                                 results[i] = i + round;
                             }
                         });
        total += std::accumulate(results.begin(), results.end(), u64{0});
        u64 expected = 0;
        for (usize i = 0; i < count; ++i)
        {
            expected += i + round;
        }
        REQUIRE(std::accumulate(results.begin(), results.end(), u64{0}) == expected);
    }
    CHECK(total > 0);

    // Many groups and systems coming and going.
    for (u32 round = 0; round < 50; ++round)
    {
        JobSystem shortLived(1 + round % 6);
        JobGroup group(shortLived);
        std::atomic<u32> ran{0};
        for (u32 i = 0; i < 40; ++i)
        {
            group.run([&] { ran.fetch_add(1); });
        }
        group.wait();
        REQUIRE(ran.load() == 40);
    }
}
