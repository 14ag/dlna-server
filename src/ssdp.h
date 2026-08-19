#ifndef SSDP_H
#define SSDP_H

#include <string>
#include <vector>
#include <deque>
#include <atomic>
#ifdef _WIN32
#include <winsock2.h>
#include <windows.h>
#endif
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>
#include "netutils.h"

struct SSDPTarget {
    std::string st;
    std::string usn;
};

struct DelayedSearchResponse {
#ifdef _WIN32
    SOCKET socket;
#else
    int socket;
#endif
    SOCKADDR_STORAGE remoteAddr;
    int remoteLen;
    NetworkEndpoint endpoint;
    std::vector<std::string> messages;
    std::vector<std::string> logSt;
    std::vector<std::string> logUsn;
    std::chrono::steady_clock::time_point dueAt;
};

class SSDP {
public:
    static SSDP& Get();

    bool Start(const std::vector<NetworkEndpoint>& endpoints, int port, const std::wstring& serverName, const std::wstring& uuid);
    void Stop();
    // true only while the search and notify worker thread that Start
    // launched is still executing mirrors HttpServer IsHealthy exactly
    bool IsHealthy() const { return m_workerThreadAlive.load(std::memory_order_acquire); }
    // test only accessor for the bootid persistence regression hook
    // cheap and harmless in production so it is not ifdef guarded
    unsigned int GetBootIdForTest() const { return m_bootId; }
    // test only queues a synthetic response due immediately then calls
    // Stop so a pytest can confirm task 3 flushes it instead of
    // dropping it silently
    void QueueTestResponseDueNow();

private:
    SSDP();

    void CloseSockets();
    void SendNotifyRound(const char* nts);
    void QueueSearchResponses(DelayedSearchResponse response);
    void SendDelayedSearchResponse(const DelayedSearchResponse& response);
    void ResponseWorker();
#ifdef _WIN32
    void SendNotifyBurst(const char* nts, int rounds, DWORD delayMs);
    void HandleSearchRequest(SOCKET socket, const SOCKADDR* remoteAddr, int remoteLen, const std::string& request);
    static DWORD WINAPI ThreadWorker(LPVOID lpParam);

    std::atomic<bool> m_running;
    HANDLE m_hThread;
    SOCKET m_ipv4Socket;
    SOCKET m_ipv6Socket;
#else
    void SendNotifyBurst(const char* nts, int rounds, unsigned int delayMs);
    void HandleSearchRequest(int socket, const SOCKADDR* remoteAddr, socklen_t remoteLen, const std::string& request);
    void ThreadWorker();

    std::atomic<bool> m_running;
    std::thread m_thread;
    int m_ipv4Socket;
    int m_ipv6Socket;
#endif
    std::thread m_responseThread;
    std::thread m_initialBurstThread;
    std::mutex m_responseMutex;
    std::condition_variable m_responseCondition;
    // deque, not vector: QueueSearchResponses() evicts the oldest queued
    // response with pop_front() once the queue hits
    // kMaxDelayedResponses. On a vector that eviction was
    // erase(begin()), an O(n) shift of every remaining element, on
    // every single eviction -- under a sustained M-SEARCH flood the
    // queue sits at the cap and every arrival evicts one, making the
    // steady-state cost O(n) per incoming search. See F-PERF-01.
    std::deque<DelayedSearchResponse> m_delayedResponses;
    // split so an ipv4 send and an ipv6 send never block each other
    // only sends on the same address family need to serialize against
    // each other and against a Stop closing that family's socket
    std::mutex m_ipv4SendMutex;
    std::mutex m_ipv6SendMutex;

    std::vector<NetworkEndpoint> m_endpoints;
    std::string m_uuidStr;
    std::vector<SSDPTarget> m_targets;
    unsigned int m_bootId;
    unsigned int m_configId;
    // true once m_bootId has been assigned for this process lifetime
    // BOOTID.UPNP.ORG must not change on a restart that is not a
    // genuine device reboot per UPnP Device Architecture 1 1 section
    // 1 2 Advertisement see the workflow document citation
    bool m_bootIdAssigned = false;
    std::atomic<bool> m_workerThreadAlive{false};
    // guards Start and Stop against re-entrant concurrent calls from a
    // second thread the same class of check-then-act race CWE-367
    // that Server::m_starting in server.cpp already guards against
    // for Server::Start a second caller fails fast and logs instead
    // of racing socket creation destruction or the m_endpoints
    // m_targets m_bootId m_configId members against the in progress
    // caller see the workflow document for the full trace
    std::atomic<bool> m_lifecycleBusy{false};
};

#endif // SSDP_H
