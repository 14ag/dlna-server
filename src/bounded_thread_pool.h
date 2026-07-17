#ifndef BOUNDED_THREAD_POOL_H
#define BOUNDED_THREAD_POOL_H

#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

class BoundedThreadPool {
public:
    // maxQueueDepth of 0 (the default) means unbounded, preserving the
    // exact old behavior for any existing caller that does not opt in.
    // Pass a nonzero value to make Submit() block the calling thread once
    // that many tasks are already queued, until a worker frees up space.
    // See Task 10 of dlna-server-concurrency-memory-fix-workflow-17-7-26.md.
    explicit BoundedThreadPool(size_t workerCount, size_t maxQueueDepth = 0);
    ~BoundedThreadPool();

    BoundedThreadPool(const BoundedThreadPool&) = delete;
    BoundedThreadPool& operator=(const BoundedThreadPool&) = delete;

    void Submit(std::function<void()> task);
    size_t WorkerCount() const { return m_workers.size(); }

private:
    void WorkerLoop();

    std::mutex m_mutex;
    std::condition_variable m_cv;
    std::condition_variable m_spaceCv;
    std::deque<std::function<void()>> m_queue;
    std::vector<std::thread> m_workers;
    bool m_stopping = false;
    size_t m_maxQueueDepth = 0;
};

#endif // BOUNDED_THREAD_POOL_H