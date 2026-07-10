#ifndef TASK_GROUP_H
#define TASK_GROUP_H

#include <condition_variable>
#include <cstddef>
#include <mutex>

class TaskGroup {
public:
    void Enter() {
        std::lock_guard<std::mutex> lock(m_mutex);
        ++m_pending;
    }

    void Leave() {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            --m_pending;
        }
        m_cv.notify_all();
    }

    void Wait() {
        std::unique_lock<std::mutex> lock(m_mutex);
        m_cv.wait(lock, [this]() { return m_pending == 0; });
    }

    size_t PendingCount() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_pending;
    }

private:
    mutable std::mutex m_mutex;
    std::condition_variable m_cv;
    size_t m_pending = 0;
};

class TaskGroupLeaveGuard {
public:
    explicit TaskGroupLeaveGuard(TaskGroup& group) : m_group(group) {}
    ~TaskGroupLeaveGuard() { m_group.Leave(); }
    TaskGroupLeaveGuard(const TaskGroupLeaveGuard&) = delete;
    TaskGroupLeaveGuard& operator=(const TaskGroupLeaveGuard&) = delete;

private:
    TaskGroup& m_group;
};

#endif // TASK_GROUP_H