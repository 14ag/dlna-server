#include "bounded_thread_pool.h"

BoundedThreadPool::BoundedThreadPool(size_t workerCount, size_t maxQueueDepth)
    : m_maxQueueDepth(maxQueueDepth) {
    if (workerCount == 0) workerCount = 1;
    m_workers.reserve(workerCount);
    for (size_t i = 0; i < workerCount; ++i) {
        m_workers.emplace_back(&BoundedThreadPool::WorkerLoop, this);
    }
}

BoundedThreadPool::~BoundedThreadPool() {
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_stopping = true;
    }
    m_cv.notify_all();
    m_spaceCv.notify_all();
    for (auto& worker : m_workers) {
        if (worker.joinable()) worker.join();
    }
}

void BoundedThreadPool::Submit(std::function<void()> task) {
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        if (m_maxQueueDepth > 0) {
            m_spaceCv.wait(lock, [this]() { return m_stopping || m_queue.size() < m_maxQueueDepth; });
            if (m_stopping) return;
        }
        m_queue.push_back(std::move(task));
    }
    m_cv.notify_one();
}

void BoundedThreadPool::WorkerLoop() {
    while (true) {
        std::function<void()> task;
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            m_cv.wait(lock, [this]() { return m_stopping || !m_queue.empty(); });
            if (m_stopping && m_queue.empty()) return;
            task = std::move(m_queue.front());
            m_queue.pop_front();
        }
        m_spaceCv.notify_one();
        task();
    }
}