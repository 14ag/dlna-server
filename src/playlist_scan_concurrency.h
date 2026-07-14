#ifndef PLAYLIST_SCAN_CONCURRENCY_H
#define PLAYLIST_SCAN_CONCURRENCY_H

#include <condition_variable>
#include <cstddef>
#include <mutex>
#include "bounded_thread_pool.h"

size_t ComputePlaylistScanConcurrency(size_t nestedM3u8Count);

class AdaptiveConcurrencyLimiter {
public:
    explicit AdaptiveConcurrencyLimiter(size_t initialLimit);

    void Acquire();
    void Release();
    void SetLimit(size_t newLimit);
    size_t CurrentLimit() const;

private:
    mutable std::mutex m_mutex;
    std::condition_variable m_cv;
    size_t m_limit;
    size_t m_active;
};

class PlaylistScanPool {
public:
    static BoundedThreadPool& Get();
};

constexpr size_t kPlaylistScanPoolSize = 20;

#endif // PLAYLIST_SCAN_CONCURRENCY_H