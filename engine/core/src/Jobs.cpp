#include <g7/core/Assert.hpp>
#include <g7/core/Jobs.hpp>
#include <g7/core/Profiler.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <exception>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

namespace g7
{
namespace
{
constexpr u32 kMaxAutoThreads = 8;
constexpr u32 kMaxThreads = 64;
constexpr usize kChunksPerThread = 4; // automatic chunk size

// Tracy thread names (string literals: they must outlive the thread).
constexpr std::array<const char*, 8> kWorkerNames = {"job worker 1", "job worker 2", "job worker 3",
                                                     "job worker 4", "job worker 5", "job worker 6",
                                                     "job worker 7", "job worker 8"};

thread_local bool t_insideJob = false;

/// Marks the current thread as working on jobs for its lifetime (nested parallelFor runs serially).
class InsideJobScope
{
public:
    InsideJobScope() noexcept : m_previous(t_insideJob) { t_insideJob = true; }
    ~InsideJobScope() { t_insideJob = m_previous; }
    InsideJobScope(const InsideJobScope&) = delete;
    InsideJobScope& operator=(const InsideJobScope&) = delete;

private:
    bool m_previous;
};

/// One parallelFor: the chunks are claimed through `next`; the caller and the helpers it queued work until
/// none is left. Shared by the queued helpers, so a helper that starts late (after the caller returned) finds
/// no chunk and never touches the caller's function.
struct Batch
{
    const JobSystem::RangeFn* fn = nullptr;
    usize count = 0;
    usize chunk = 1;
    usize chunks = 0;
    std::atomic<usize> next{0};
    std::atomic<usize> finished{0};
    std::atomic<bool> failed{false};
    std::mutex mutex; // guards error; with `done` for the caller's wait
    std::condition_variable done;
    std::exception_ptr error;

    void work()
    {
        for (;;)
        {
            const usize index = next.fetch_add(1);
            if (index >= chunks)
            {
                return;
            }
            if (!failed.load(std::memory_order_relaxed))
            {
                const usize begin = index * chunk;
                try
                {
                    (*fn)(begin, std::min(count, begin + chunk));
                }
                catch (...)
                {
                    const std::lock_guard lock(mutex);
                    if (!error)
                    {
                        error = std::current_exception();
                    }
                    failed.store(true, std::memory_order_relaxed);
                }
            }
            if (finished.fetch_add(1) + 1 == chunks)
            {
                const std::lock_guard lock(mutex); // the caller is either before its check or waiting
                done.notify_all();
            }
        }
    }
};
} // namespace

struct JobSystem::Impl
{
    u32 threads = 1;
    std::mutex mutex; // guards queue and stopping
    std::condition_variable available;
    std::deque<std::function<void()>> queue;
    bool stopping = false;
    std::vector<std::jthread> pool;

    void push(std::function<void()> job)
    {
        {
            const std::lock_guard lock(mutex);
            queue.push_back(std::move(job));
        }
        available.notify_one();
    }

    void pushMany(std::function<void()> job, usize copies)
    {
        {
            const std::lock_guard lock(mutex);
            for (usize i = 0; i < copies; ++i)
            {
                queue.push_back(job);
            }
        }
        if (copies == 1)
        {
            available.notify_one();
        }
        else
        {
            available.notify_all();
        }
    }

    [[nodiscard]] bool tryPop(std::function<void()>& job)
    {
        const std::lock_guard lock(mutex);
        if (queue.empty())
        {
            return false;
        }
        job = std::move(queue.front());
        queue.pop_front();
        return true;
    }

    void workerLoop(u32 index)
    {
        G7_PROFILE_THREAD(index < kWorkerNames.size() ? kWorkerNames[index] : "job worker");
        static_cast<void>(index);
        t_insideJob = true;
        for (;;)
        {
            std::function<void()> job;
            {
                std::unique_lock lock(mutex);
                available.wait(lock, [this] { return stopping || !queue.empty(); });
                if (stopping)
                {
                    return;
                }
                job = std::move(queue.front());
                queue.pop_front();
            }
            G7_PROFILE_SCOPE("job");
            job(); // jobs catch their own exceptions
        }
    }
};

JobSystem::JobSystem(u32 threads) : m_impl(std::make_unique<Impl>())
{
    m_impl->threads = threads == 0 ? autoThreads() : std::min(threads, kMaxThreads);
    m_impl->pool.reserve(m_impl->threads - 1);
    for (u32 i = 0; i + 1 < m_impl->threads; ++i)
    {
        m_impl->pool.emplace_back([impl = m_impl.get(), i] { impl->workerLoop(i); });
    }
}

JobSystem::~JobSystem()
{
    {
        const std::lock_guard lock(m_impl->mutex);
        m_impl->stopping = true;
    }
    m_impl->available.notify_all();
    m_impl->pool.clear(); // joins
}

u32 JobSystem::autoThreads() noexcept
{
    const u32 cores = std::thread::hardware_concurrency();
    return std::clamp<u32>(cores > 1 ? cores - 1 : 1, 1, kMaxAutoThreads);
}

u32 JobSystem::threads() const noexcept
{
    return m_impl->threads;
}

u32 JobSystem::workers() const noexcept
{
    return m_impl->threads - 1;
}

bool JobSystem::insideJob() noexcept
{
    return t_insideJob;
}

void JobSystem::parallelFor(usize count, usize chunk, const RangeFn& fn)
{
    if (count == 0)
    {
        return;
    }
    if (chunk == 0)
    {
        const usize parts = static_cast<usize>(m_impl->threads) * kChunksPerThread;
        chunk = std::max<usize>(1, (count + parts - 1) / parts);
    }
    const usize chunks = (count + chunk - 1) / chunk;

    // Serial: no workers, nested call or a single chunk. Same chunks, in order, on this thread.
    if (m_impl->threads == 1 || t_insideJob || chunks == 1)
    {
        const InsideJobScope inside;
        std::exception_ptr error;
        for (usize begin = 0; begin < count; begin += chunk)
        {
            try
            {
                fn(begin, std::min(count, begin + chunk));
            }
            catch (...)
            {
                error = std::current_exception();
                break;
            }
        }
        if (error)
        {
            std::rethrow_exception(error);
        }
        return;
    }

    auto batch = std::make_shared<Batch>();
    batch->fn = &fn;
    batch->count = count;
    batch->chunk = chunk;
    batch->chunks = chunks;
    const usize helpers = std::min<usize>(m_impl->threads - 1, chunks - 1);
    m_impl->pushMany([batch] { batch->work(); }, helpers);
    {
        const InsideJobScope inside;
        batch->work();
    }
    std::unique_lock lock(batch->mutex);
    batch->done.wait(lock, [&] { return batch->finished.load() == batch->chunks; });
    if (batch->error)
    {
        std::rethrow_exception(batch->error);
    }
}

struct JobGroup::State
{
    std::mutex mutex; // guards pending and error
    std::condition_variable idle;
    usize pending = 0;
    std::exception_ptr error;

    void fail(std::exception_ptr exception)
    {
        const std::lock_guard lock(mutex);
        if (!error)
        {
            error = std::move(exception);
        }
    }
};

JobGroup::JobGroup(JobSystem& jobs) : m_jobs(jobs), m_state(std::make_shared<State>())
{
}

JobGroup::~JobGroup()
{
    try
    {
        wait();
    }
    catch (...) // NOLINT(bugprone-empty-catch): documented, wait() reports it
    {
    }
}

void JobGroup::run(std::function<void()> job)
{
    G7_ASSERT(job, "JobGroup::run without a job");
    if (m_jobs.workers() == 0 || t_insideJob)
    {
        const InsideJobScope inside;
        try
        {
            job();
        }
        catch (...)
        {
            m_state->fail(std::current_exception());
        }
        return;
    }
    {
        const std::lock_guard lock(m_state->mutex);
        ++m_state->pending;
    }
    m_jobs.m_impl->push(
        [state = m_state, job = std::move(job)]
        {
            try
            {
                job();
            }
            catch (...)
            {
                state->fail(std::current_exception());
            }
            const std::lock_guard lock(
                state->mutex); // notify under the lock: the waiter may destroy the group
            if (--state->pending == 0)
            {
                state->idle.notify_all();
            }
        });
}

void JobGroup::wait()
{
    // Help with queued jobs (this group's or others') while the group is busy, then sleep until it is done.
    for (;;)
    {
        {
            const std::lock_guard lock(m_state->mutex);
            if (m_state->pending == 0)
            {
                break;
            }
        }
        std::function<void()> job;
        if (m_jobs.m_impl->tryPop(job))
        {
            const InsideJobScope inside;
            job();
            continue;
        }
        std::unique_lock lock(m_state->mutex);
        m_state->idle.wait(lock, [this] { return m_state->pending == 0; });
        break;
    }
    std::exception_ptr error;
    {
        const std::lock_guard lock(m_state->mutex);
        error = std::exchange(m_state->error, nullptr);
    }
    if (error)
    {
        std::rethrow_exception(error);
    }
}
} // namespace g7
