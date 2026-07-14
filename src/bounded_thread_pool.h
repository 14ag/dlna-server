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
    explicit BoundedThreadPool(size_t workerCount);
    ~BoundedThreadPool();

    BoundedThreadPool(const BoundedThreadPool&) = delete;
    BoundedThreadPool& operator=(const BoundedThreadPool&) = delete;

    void Submit(std::function<void()> task);
    size_t WorkerCount() const { return m_workers.size(); }

private:
    void WorkerLoop();

    std::mutex m_mutex;
    std::condition_variable m_cv;
    std::deque<std::function<void()>> m_queue;
    std::vector<std::thread> m_workers;
    bool m_stopping = false;
};

#endif // BOUNDED_THREAD_POOL_H