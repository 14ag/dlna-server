#ifndef UPNP_EVENTING_H
#define UPNP_EVENTING_H

#include "bounded_thread_pool.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>

inline constexpr size_t kMaxUpnpSubscriptions = 64;

// GENA NOTIFY delivery is a background best effort job not a per
// subscriber guarantee sizing the delivery pool to kMaxUpnpSubscriptions
// would spawn up to 64 os threads the first time even one renderer ever
// subscribes see the workflow document task 10 for the reasoning and the
// reference implementation this size is compared against
inline constexpr size_t kMaxUpnpNotifyWorkers = 8;

class UpnpEventManager {
public:
    static UpnpEventManager& Get();

    std::string HandleEventSubscription(const std::string& method,
                                        const std::string& path,
                                        const std::string& request);
    std::string RegisterSubscription(const std::string& servicePath,
                                     const std::string& callbackHeader,
                                     int timeoutSeconds);
    bool RenewSubscription(const std::string& sid, int timeoutSeconds);
    bool RemoveSubscription(const std::string& sid);
    void ClearSubscriptions();
    void NotifySystemUpdateId(int updateId);

private:
    struct Subscription {
        std::string sid;
        std::string servicePath;
        std::string callbackUrl;
        unsigned int sequence = 0;
        std::chrono::steady_clock::time_point expiresAt;
    };

    struct NotifyJob {
        std::string callbackUrl;
        std::string sid;
        unsigned int sequence = 0;
        unsigned long long generation = 0;
        std::string body;
    };

    UpnpEventManager();
    ~UpnpEventManager();
    UpnpEventManager(const UpnpEventManager&) = delete;
    UpnpEventManager& operator=(const UpnpEventManager&) = delete;

    void StartWorkerLocked();
    void QueueInitialNotifyLocked(const Subscription& subscription);
    void QueueNotifyJobLocked(NotifyJob job);
    void DispatchNotifyToSubscribersLocked(int updateId, std::chrono::steady_clock::time_point now);
    void ExpireSubscription(const std::string& sid);
    void StopWorker();
    void WorkerLoop();
    void SendNotifyJob(const NotifyJob& job);

    std::mutex m_mutex;
    std::condition_variable m_cv;
    std::unordered_map<std::string, Subscription> m_subscriptions;
    // Mirrors m_subscriptions.size(), updated under m_mutex at every point
    // m_subscriptions is mutated. Read with no lock from
    // NotifySystemUpdateId() so a scan publishing thousands of items while
    // nobody is subscribed does not pay for m_mutex at all. See Task 6 of
    // dlna-server-concurrency-memory-fix-workflow-17-7-26.md.
    std::atomic<size_t> m_subscriberCount{0};
    std::deque<NotifyJob> m_queue;
    std::thread m_worker;
    bool m_stopping = false;
    bool m_workerStarted = false;
    // Bounded so a burst of subscribers cannot spawn unbounded concurrent
    // outbound HTTP NOTIFY requests; sized to kMaxUpnpSubscriptions since
    // that is already the hard cap on how many jobs could ever be in
    // flight at once in the worst case (one per subscriber).
    std::unique_ptr<BoundedThreadPool> m_notifyPool;
    // Atomic, not plain int: NotifySystemUpdateId()'s fast path (see Task 6
    // of dlna-server-concurrency-memory-fix-workflow-17-7-26.md) writes
    // this with no lock held whenever there are zero subscribers, so every
    // other read/write of this field must also go through the atomic
    // interface, never direct assignment.
    std::atomic<int> m_lastSystemUpdateId{1};
    unsigned long long m_generation = 0;
    // GENA notify moderation (leading-edge-plus-trailing-edge debounce): a
    // publish burst dispatches its first update immediately, then coalesces
    // every update inside m_minNotifyInterval into one trailing dispatch of
    // the latest updateId. This bounds DispatchNotifyToSubscribersLocked's
    // O(subscriber count) mutex-held iteration to roughly once per window
    // instead of once per PublishItem/PublishContainer call.
    std::chrono::milliseconds m_minNotifyInterval{500};
    std::chrono::steady_clock::time_point m_lastDispatchTime{};
    bool m_trailingFirePending = false;
    int m_trailingUpdateId = 0;
    std::chrono::steady_clock::time_point m_trailingFireAt{};
    std::atomic<unsigned long long> m_sidCounter;
 };

#define AppEvents UpnpEventManager::Get()

#endif // UPNP_EVENTING_H