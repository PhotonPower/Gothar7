#pragma once

#include <g7/core/Types.hpp>

#include <functional>
#include <memory>

/// A small job system (docs/modules/perf.md part C): a fixed pool of worker threads with one queue,
/// parallelFor over index ranges and groups of independent jobs. No task graphs, no work stealing.
///
/// Rules for the code that runs in jobs: no Lua (sol2 is not thread-safe), no random numbers, no writes to
/// shared data - every job writes into its own result slot and the calling thread hands the results on in a
/// fixed order. Exceptions thrown by a job are caught and rethrown on the waiting thread (the first one; the
/// others are dropped).
namespace g7
{
class JobSystem
{
public:
    /// The work of one parallelFor chunk: indices [begin, end).
    using RangeFn = std::function<void(usize begin, usize end)>;

    /// `threads` = how many threads work on a parallelFor, the calling thread included (workers = threads -
    /// 1). 0 = automatic (autoThreads()); 1 = everything runs serially on the calling thread, no workers.
    explicit JobSystem(u32 threads = 0);
    ~JobSystem();

    JobSystem(const JobSystem&) = delete;
    JobSystem& operator=(const JobSystem&) = delete;

    /// The automatic thread count: logical cores - 1 (the rest of the machine keeps one), at least 1, at
    /// most 8.
    [[nodiscard]] static u32 autoThreads() noexcept;

    /// Threads working on a parallelFor, the calling thread included (>= 1).
    [[nodiscard]] u32 threads() const noexcept;
    /// Worker threads of the pool (threads() - 1).
    [[nodiscard]] u32 workers() const noexcept;

    /// Calls `fn(begin, end)` for the chunks [0, chunk), [chunk, 2 chunk), ... of [0, count) - every index
    /// exactly once, in chunks of `chunk` indices (the last one may be shorter; 0 = automatic, about 4 chunks
    /// per thread). The calling thread works along and returns when every chunk is done. Serial (chunks in
    /// order on the calling thread) with threads() == 1 and when called from inside a job (nested). Rethrows
    /// the first exception of a chunk after all chunks have finished; chunks not yet started when it was
    /// thrown are skipped.
    void parallelFor(usize count, usize chunk, const RangeFn& fn);

    /// True on a worker thread and on a thread that is inside parallelFor or JobGroup work.
    [[nodiscard]] static bool insideJob() noexcept;

private:
    friend class JobGroup;
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

/// Independent jobs that are waited for together. run() queues a job (runs it at once when the system has no
/// workers or when called from inside a job); wait() works on queued jobs until the group's jobs are done and
/// rethrows the first exception. The destructor waits as well but drops an exception - call wait().
class JobGroup
{
public:
    explicit JobGroup(JobSystem& jobs);
    ~JobGroup();

    JobGroup(const JobGroup&) = delete;
    JobGroup& operator=(const JobGroup&) = delete;

    void run(std::function<void()> job);
    void wait();

private:
    struct State;
    JobSystem& m_jobs;
    std::shared_ptr<State> m_state;
};
} // namespace g7
