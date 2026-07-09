#ifndef UPNP_EVENTING_H
#define UPNP_EVENTING_H

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>

inline constexpr size_t kMaxUpnpSubscriptions = 64;

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
    void ExpireSubscription(const std::string& sid);
    void StopWorker();
    void WorkerLoop();
    void SendNotifyJob(const NotifyJob& job);

    std::mutex m_mutex;
    std::condition_variable m_cv;
    std::unordered_map<std::string, Subscription> m_subscriptions;
    std::deque<NotifyJob> m_queue;
    std::thread m_worker;
    bool m_stopping = false;
    bool m_workerStarted = false;
    int m_lastSystemUpdateId = 1;
    unsigned long long m_generation = 0;
    std::atomic<unsigned long long> m_sidCounter;
};

#define AppEvents UpnpEventManager::Get()

#endif // UPNP_EVENTING_H
