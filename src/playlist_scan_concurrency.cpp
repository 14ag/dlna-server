#include "playlist_scan_concurrency.h"
#include <algorithm>
#include <cmath>
#include <thread>
#include "http_common.h"

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
    static BoundedThreadPool instance(kPlaylistScanPoolSize, kPlaylistScanPoolMaxQueueDepth);
    return instance;
}

namespace {
size_t ComputeSourceScanPoolWorkerCount() {
    const unsigned int hw = std::thread::hardware_concurrency();
    const size_t base = hw == 0 ? 4 : static_cast<size_t>(hw);
    // per source scan jobs are i o bound disk stat listdir or ftp http
    // network calls more than cpu bound so oversubscribing relative to
    // core count lets more sources overlap while their i o is in flight
    // the same reasoning civetweb documents for its own num_threads
    // default and that this project's own httpserver cpp already cites
    // for its windows threadpool sizing capped at kMaxClientThreads so a
    // machine with an unusually high core count still gets a bounded
    // pool rather than one that scales without limit see F-PERF-04
    return (std::min)(base * 2, kMaxClientThreads);
}
}

BoundedThreadPool& SourceScanPool::Get() {
    static BoundedThreadPool instance(ComputeSourceScanPoolWorkerCount());
    return instance;
}