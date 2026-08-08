#ifndef NETWORK_CHANGE_DEBOUNCE_H
#define NETWORK_CHANGE_DEBOUNCE_H

#include <chrono>
#include <mutex>

class NetworkChangeDebouncer {
public:
    explicit NetworkChangeDebouncer(std::chrono::milliseconds minInterval)
        : m_minInterval(minInterval) {}

    bool OnChangeShouldActNow(std::chrono::steady_clock::time_point now) {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (now - m_lastActedAt >= m_minInterval) {
            m_lastActedAt = now;
            m_trailingPending = false;
            return true;
        }
        m_trailingPending = true;
        m_trailingDeadline = m_lastActedAt + m_minInterval;
        return false;
    }

    bool TrailingPending() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_trailingPending;
    }

    std::chrono::steady_clock::time_point TrailingDeadline() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_trailingDeadline;
    }

    void MarkTrailingHandled(std::chrono::steady_clock::time_point now) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_lastActedAt = now;
        m_trailingPending = false;
    }

private:
    mutable std::mutex m_mutex;
    std::chrono::milliseconds m_minInterval;
    std::chrono::steady_clock::time_point m_lastActedAt{};
    bool m_trailingPending = false;
    std::chrono::steady_clock::time_point m_trailingDeadline{};
};

#endif
