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
    std::mutex m_socketMutex;

    std::vector<NetworkEndpoint> m_endpoints;
    std::string m_uuidStr;
    std::vector<SSDPTarget> m_targets;
    unsigned int m_bootId;
    unsigned int m_configId;
    std::atomic<bool> m_workerThreadAlive{false};
};

#endif // SSDP_H
