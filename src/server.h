#ifndef SERVER_H
#define SERVER_H

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <string>
#include <vector>
#include <thread>
#include "netutils.h"

struct ConfigSnapshot;

class Server {
public:
    static Server& Get();

    bool Start(std::wstring& outReason);
    void Stop();
    bool Rescan();
    
    bool IsRunning() const { return m_running.load(std::memory_order_acquire); }
    bool IsInitialScanComplete() const { return m_initialScanComplete.load(std::memory_order_acquire); }
    bool IsInitialScanInProgress() const { return m_initialScanInProgress.load(std::memory_order_acquire); }
    std::wstring GetEndpoint() const;
    std::vector<NetworkEndpoint> GetEndpoints() const;

private:
    Server();
    ~Server();
    void RefreshEndpoints(const ConfigSnapshot& cfg);
    bool ShouldStartScan() const;
    void StartBackgroundScan();
    void JoinBackgroundScan();
    void JoinBackgroundScanLocked();
    void StartWatchMode();
    void StopWatchMode();
    void WatchLoop();

    std::atomic<bool> m_running;
    std::atomic<bool> m_stopping;
    std::atomic<bool> m_initialScanComplete;   // becomes: "root container exists"
    std::atomic<bool> m_initialScanInProgress; // NEW: true while the very first scan runs
    std::wstring m_endpoint;
    std::vector<NetworkEndpoint> m_endpoints;
    std::thread m_scanThread;
    std::thread m_watchThread;
    std::mutex m_scanMutex;
    std::mutex m_rescanMutex;
    mutable std::mutex m_endpointMutex;
    std::mutex m_watchMutex;
    std::condition_variable m_watchCv;
    std::atomic<bool> m_stopWatch;
};

#define DLNAServer Server::Get()

#endif // SERVER_H
