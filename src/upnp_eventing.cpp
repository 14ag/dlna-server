#include "upnp_eventing.h"

#include "dlna_utils.h"
#include "log.h"

#include <algorithm>
#include <cctype>
#include <iomanip>
#include <random>
#include <sstream>
#include <vector>

#ifdef DLNA_HAS_LIBCURL
#include <curl/curl.h>
#endif

namespace {
constexpr int kDefaultTimeoutSeconds = 1800;
constexpr int kMaxTimeoutSeconds = 86400;
constexpr size_t kMaxQueuedNotifyJobs = 256;

bool IsEventPath(const std::string& path) {
    return path == "/upnp/event/content_directory" || path == "/upnp/event/connection_manager";
}

bool IsContentDirectoryEventPath(const std::string& path) {
    return path == "/upnp/event/content_directory";
}

std::string LowerAsciiLocal(std::string value) {
    for (char& ch : value) {
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    return value;
}

std::string ParseCallbackUrl(std::string value) {
    value = TrimAscii(value);
    const size_t left = value.find('<');
    const size_t right = value.find('>', left == std::string::npos ? 0 : left + 1);
    if (left != std::string::npos && right != std::string::npos && right > left + 1) {
        value = value.substr(left + 1, right - left - 1);
    }
    value = TrimAscii(value);
    std::string lower = LowerAsciiLocal(value);
    if (lower.rfind("http://", 0) != 0) {
        return {};
    }
    return value;
}

int ParseTimeoutSeconds(const std::string& header) {
    std::string value = TrimAscii(header);
    if (value.empty()) return kDefaultTimeoutSeconds;
    if (LowerAsciiLocal(value) == "infinite") return kMaxTimeoutSeconds;
    const std::string prefix = "second-";
    std::string lower = LowerAsciiLocal(value);
    if (lower.rfind(prefix, 0) != 0) return kDefaultTimeoutSeconds;

    long long parsed = 0;
    if (!TryParseNonNegativeLongLong(value.substr(prefix.size()), parsed)) {
        return kDefaultTimeoutSeconds;
    }
    if (parsed <= 0) return kDefaultTimeoutSeconds;
    return static_cast<int>((std::min)(parsed, static_cast<long long>(kMaxTimeoutSeconds)));
}

std::string PreconditionFailed() {
    return "HTTP/1.1 412 Precondition Failed\r\nConnection: close\r\nContent-Length: 0\r\n\r\n";
}

std::string NotFound() {
    return "HTTP/1.1 404 Not Found\r\nConnection: close\r\nContent-Length: 0\r\n\r\n";
}

std::string OkWithSubscription(const std::string& sid, int timeoutSeconds) {
    return "HTTP/1.1 200 OK\r\n"
           "SID: " + sid + "\r\n"
           "TIMEOUT: Second-" + std::to_string(timeoutSeconds) + "\r\n"
           "Connection: close\r\n"
           "Content-Length: 0\r\n\r\n";
}

std::string OkNoBody() {
    return "HTTP/1.1 200 OK\r\nConnection: close\r\nContent-Length: 0\r\n\r\n";
}

std::string MakeSystemUpdateBody(int updateId) {
    return "<?xml version=\"1.0\" encoding=\"utf-8\"?>"
           "<e:propertyset xmlns:e=\"urn:schemas-upnp-org:event-1-0\">"
           "<e:property><SystemUpdateID>" + std::to_string(updateId) + "</SystemUpdateID></e:property>"
           "</e:propertyset>";
}

std::string MakeSid(unsigned long long counter) {
    std::random_device random;
    unsigned int parts[4] = { random(), random(), random(), random() };
    std::ostringstream ss;
    ss << "uuid:" << std::hex << std::setfill('0')
       << std::setw(8) << parts[0] << "-"
       << std::setw(4) << ((parts[1] >> 16) & 0xffff) << "-"
       << std::setw(4) << (parts[1] & 0xffff) << "-"
       << std::setw(4) << ((parts[2] >> 16) & 0xffff) << "-"
       << std::setw(4) << (parts[2] & 0xffff)
       << std::setw(8) << static_cast<unsigned int>(counter & 0xffffffffULL);
    return ss.str();
}

struct CurlGlobalInit {
    CurlGlobalInit() { curl_global_init(CURL_GLOBAL_DEFAULT); }
    ~CurlGlobalInit() { curl_global_cleanup(); }
};
}

UpnpEventManager& UpnpEventManager::Get() {
    static UpnpEventManager instance;
    return instance;
}

UpnpEventManager::UpnpEventManager() : m_sidCounter(1) {
}

UpnpEventManager::~UpnpEventManager() {
    StopWorker();
}

std::string UpnpEventManager::HandleEventSubscription(const std::string& method,
                                                      const std::string& path,
                                                      const std::string& request) {
    if (!IsEventPath(path)) return NotFound();

    const std::string sid = FindHeaderValueCaseInsensitive(request, "SID");
    const int timeoutSeconds = ParseTimeoutSeconds(FindHeaderValueCaseInsensitive(request, "TIMEOUT"));
    if (method == "UNSUBSCRIBE") {
        if (sid.empty()) return PreconditionFailed();
        RemoveSubscription(sid);
        return OkNoBody();
    }

    if (method != "SUBSCRIBE") return PreconditionFailed();
    if (!sid.empty()) {
        return RenewSubscription(sid, timeoutSeconds) ? OkWithSubscription(sid, timeoutSeconds) : PreconditionFailed();
    }

    const std::string callback = FindHeaderValueCaseInsensitive(request, "CALLBACK");
    const std::string nt = LowerAsciiLocal(FindHeaderValueCaseInsensitive(request, "NT"));
    if (callback.empty() || nt != "upnp:event") return PreconditionFailed();

    const std::string newSid = RegisterSubscription(path, callback, timeoutSeconds);
    if (newSid.empty()) return PreconditionFailed();
    return OkWithSubscription(newSid, timeoutSeconds);
}

std::string UpnpEventManager::RegisterSubscription(const std::string& servicePath,
                                                    const std::string& callbackHeader,
                                                    int timeoutSeconds) {
    const std::string callbackUrl = ParseCallbackUrl(callbackHeader);
    if (callbackUrl.empty() || !IsEventPath(servicePath)) return {};

    std::lock_guard<std::mutex> lock(m_mutex);

    if (m_subscriptions.size() >= kMaxUpnpSubscriptions) {
        auto oldest = std::min_element(
            m_subscriptions.begin(), m_subscriptions.end(),
            [](const auto& a, const auto& b) { return a.second.expiresAt < b.second.expiresAt; });
        if (oldest != m_subscriptions.end()) {
            LogPrint(L"GENA subscription table full (%zu); evicting nearest-to-expire SID %hs",
                     kMaxUpnpSubscriptions, oldest->first.c_str());
            m_subscriptions.erase(oldest);
        }
    }

    const std::string sid = MakeSid(m_sidCounter.fetch_add(1, std::memory_order_relaxed));
    Subscription subscription;
    subscription.sid = sid;
    subscription.servicePath = servicePath;
    subscription.callbackUrl = callbackUrl;
    subscription.expiresAt = std::chrono::steady_clock::now() + std::chrono::seconds(timeoutSeconds);
    m_subscriptions[sid] = subscription;
    m_subscriberCount.store(m_subscriptions.size(), std::memory_order_release);
    QueueInitialNotifyLocked(m_subscriptions[sid]);
    if (!m_queue.empty()) {
        StartWorkerLocked();
        m_cv.notify_one();
    }
    return sid;
}

bool UpnpEventManager::RenewSubscription(const std::string& sid, int timeoutSeconds) {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto found = m_subscriptions.find(sid);
    if (found == m_subscriptions.end()) return false;
    found->second.expiresAt = std::chrono::steady_clock::now() + std::chrono::seconds(timeoutSeconds);
    return true;
}

bool UpnpEventManager::RemoveSubscription(const std::string& sid) {
    std::lock_guard<std::mutex> lock(m_mutex);
    const bool erased = m_subscriptions.erase(sid) > 0;
    m_subscriberCount.store(m_subscriptions.size(), std::memory_order_release);
    return erased;
}

void UpnpEventManager::ClearSubscriptions() {
    std::lock_guard<std::mutex> lock(m_mutex);
    ++m_generation;
    m_subscriptions.clear();
    m_subscriberCount.store(0, std::memory_order_release);
    m_queue.clear();
    m_trailingFirePending = false;
    m_lastDispatchTime = (std::chrono::steady_clock::time_point::min)();
}

void UpnpEventManager::DispatchNotifyToSubscribersLocked(int updateId, std::chrono::steady_clock::time_point now) {
    const std::string body = MakeSystemUpdateBody(updateId);
    for (auto it = m_subscriptions.begin(); it != m_subscriptions.end();) {
        if (it->second.expiresAt <= now) {
            it = m_subscriptions.erase(it);
            m_subscriberCount.store(m_subscriptions.size(), std::memory_order_release);
            continue;
        }
        if (IsContentDirectoryEventPath(it->second.servicePath)) {
            NotifyJob job;
            job.callbackUrl = it->second.callbackUrl;
            job.sid = it->second.sid;
            job.sequence = it->second.sequence++;
            job.generation = m_generation;
            job.body = body;
            QueueNotifyJobLocked(std::move(job));
        }
        ++it;
    }
}

void UpnpEventManager::NotifySystemUpdateId(int updateId) {
    // Fast path: this field always needs to reflect the latest updateId,
    // even while nobody is subscribed, so a renderer that subscribes later
    // sees the correct value in QueueInitialNotifyLocked(). Updating this
    // atomic costs nothing meaningful compared to taking m_mutex below, so
    // it is done unconditionally, before the subscriber-count check.
    m_lastSystemUpdateId.store(updateId, std::memory_order_relaxed);
    if (m_subscriberCount.load(std::memory_order_acquire) == 0) {
        return;
    }

    std::lock_guard<std::mutex> lock(m_mutex);

    const auto now = std::chrono::steady_clock::now();
    if (now - m_lastDispatchTime >= m_minNotifyInterval) {
        // Leading edge: window has elapsed, dispatch now and start a new window.
        m_lastDispatchTime = now;
        m_trailingFirePending = false;
        DispatchNotifyToSubscribersLocked(updateId, now);
    } else {
        // Inside the window: remember only the latest updateId. If no
        // trailing fire is scheduled yet for this window, schedule one for
        // the moment the window ends.
        m_trailingUpdateId = updateId;
        if (!m_trailingFirePending) {
            m_trailingFirePending = true;
            m_trailingFireAt = m_lastDispatchTime + m_minNotifyInterval;
        }
    }

    if (!m_queue.empty() || m_trailingFirePending) {
        StartWorkerLocked();
        m_cv.notify_one();
    }
}

void UpnpEventManager::StartWorkerLocked() {
    if (m_workerStarted) return;
    m_workerStarted = true;
    m_worker = std::thread(&UpnpEventManager::WorkerLoop, this);
}

void UpnpEventManager::QueueInitialNotifyLocked(const Subscription& subscription) {
    if (!IsContentDirectoryEventPath(subscription.servicePath)) return;
    NotifyJob job;
    job.callbackUrl = subscription.callbackUrl;
    job.sid = subscription.sid;
    job.sequence = 0;
    job.generation = m_generation;
    job.body = MakeSystemUpdateBody(m_lastSystemUpdateId.load(std::memory_order_relaxed));
    QueueNotifyJobLocked(std::move(job));
}

void UpnpEventManager::QueueNotifyJobLocked(NotifyJob job) {
    auto duplicate = std::find_if(m_queue.begin(), m_queue.end(), [&](const NotifyJob& queued) {
        return queued.sid == job.sid;
    });
    if (duplicate != m_queue.end()) {
        *duplicate = std::move(job);
        LogPrint(L"GENA notify coalesced for %hs", duplicate->sid.c_str());
        return;
    }
    if (m_queue.size() >= kMaxQueuedNotifyJobs) {
        LogPrint(L"GENA notify queue coalesced by dropping oldest job.");
        m_queue.pop_front();
    }
    m_queue.push_back(std::move(job));
}

void UpnpEventManager::ExpireSubscription(const std::string& sid) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_subscriptions.erase(sid);
    m_subscriberCount.store(m_subscriptions.size(), std::memory_order_release);
}

void UpnpEventManager::StopWorker() {
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_stopping = true;
        ++m_generation;
        m_subscriptions.clear();
        m_queue.clear();
        m_trailingFirePending = false;
    }
    m_cv.notify_all();
    if (m_worker.joinable()) m_worker.join();
    m_notifyPool.reset();
}

void UpnpEventManager::WorkerLoop() {
    while (true) {
        NotifyJob job;
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            for (;;) {
                if (m_stopping && m_queue.empty()) return;
                if (!m_queue.empty()) break;
                if (m_trailingFirePending) {
                    const auto now = std::chrono::steady_clock::now();
                    if (now >= m_trailingFireAt) {
                        m_lastDispatchTime = now;
                        m_trailingFirePending = false;
                        DispatchNotifyToSubscribersLocked(m_trailingUpdateId, now);
                        continue;
                    }
                    m_cv.wait_until(lock, m_trailingFireAt);
                    continue;
                }
                m_cv.wait(lock, [&]() { return m_stopping || !m_queue.empty() || m_trailingFirePending; });
            }
            job = m_queue.front();
            m_queue.pop_front();
            if (job.generation != m_generation) {
                continue;
            }
            if (m_subscriptions.find(job.sid) == m_subscriptions.end()) {
                continue;
            }
        }
        if (!m_notifyPool) {
            m_notifyPool = std::make_unique<BoundedThreadPool>(kMaxUpnpSubscriptions);
        }
        m_notifyPool->Submit([this, job]() { SendNotifyJob(job); });
    }
}

void UpnpEventManager::SendNotifyJob(const NotifyJob& job) {
    static CurlGlobalInit init;
    (void)init;

    CURL* curl = curl_easy_init();
    if (!curl) {
        LogPrint(L"GENA notify failed: libcurl handle creation failed.");
        return;
    }

    struct curl_slist* headers = nullptr;
    const std::string contentType = "Content-Type: text/xml; charset=\"utf-8\"";
    const std::string nt = "NT: upnp:event";
    const std::string nts = "NTS: upnp:propchange";
    const std::string sid = "SID: " + job.sid;
    const std::string seq = "SEQ: " + std::to_string(job.sequence);
    headers = curl_slist_append(headers, contentType.c_str());
    headers = curl_slist_append(headers, nt.c_str());
    headers = curl_slist_append(headers, nts.c_str());
    headers = curl_slist_append(headers, sid.c_str());
    headers = curl_slist_append(headers, seq.c_str());

    char errorBuffer[CURL_ERROR_SIZE] = {};
    curl_easy_setopt(curl, CURLOPT_URL, job.callbackUrl.c_str());
    curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "NOTIFY");
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, job.body.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE_LARGE, static_cast<curl_off_t>(job.body.size()));
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 5L);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, errorBuffer);

    CURLcode code = curl_easy_perform(curl);
    long responseCode = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &responseCode);
    if (code != CURLE_OK) {
        LogPrint(L"GENA notify failed for %hs: %hs", job.callbackUrl.c_str(), errorBuffer[0] ? errorBuffer : curl_easy_strerror(code));
        ExpireSubscription(job.sid);
    } else if (responseCode >= 400) {
        LogPrint(L"GENA notify failed for %hs: HTTP %ld", job.callbackUrl.c_str(), responseCode);
        ExpireSubscription(job.sid);
    }
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
}
