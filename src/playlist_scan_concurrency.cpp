#include "playlist_scan_concurrency.h"
#include <algorithm>
#include <cmath>

namespace {
constexpr size_t kMinConcurrency = 1;
constexpr size_t kMaxConcurrency = 20;
constexpr size_t kReferenceCount = 200;
}

size_t ComputePlaylistScanConcurrency(size_t nestedM3u8Count) {
    if (nestedM3u8Count <= 1) return kMinConcurrency;
    const double k = static_cast<double>(kMaxConcurrency - kMinConcurrency) /
                      std::log(static_cast<double>(kReferenceCount));
    const double raw = kMinConcurrency +
                        std::floor(k * std::log(static_cast<double>(nestedM3u8Count)));
    const size_t value = static_cast<size_t>(std::max(0.0, raw));
    return std::clamp(value, kMinConcurrency, kMaxConcurrency);
}

AdaptiveConcurrencyLimiter::AdaptiveConcurrencyLimiter(size_t initialLimit)
    : m_limit(initialLimit == 0 ? 1 : initialLimit), m_active(0) {}

void AdaptiveConcurrencyLimiter::Acquire() {
    std::unique_lock<std::mutex> lock(m_mutex);
    m_cv.wait(lock, [this]() { return m_active < m_limit; });
    ++m_active;
}

void AdaptiveConcurrencyLimiter::Release() {
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        --m_active;
    }
    m_cv.notify_one();
}

void AdaptiveConcurrencyLimiter::SetLimit(size_t newLimit) {
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_limit = newLimit == 0 ? 1 : newLimit;
    }
    m_cv.notify_all();
}

size_t AdaptiveConcurrencyLimiter::CurrentLimit() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_limit;
}

BoundedThreadPool& PlaylistScanPool::Get() {
    static BoundedThreadPool instance(kPlaylistScanPoolSize);
    return instance;
}