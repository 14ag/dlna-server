#ifndef SERVER_H
#define SERVER_H

#include <atomic>
#include <chrono>
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
    // re registers SSDP after a detected network topology change
    // adapter added removed IP change or a resume from suspend
    // serializes against itself and coalesces near simultaneous
    // callers see the workflow document Task 5 for the two
    // independent windows ip helper api registrations plus
    // WM_POWERBROADCAST this guards against
    void RestartSsdpForNetworkChange();
    
    bool IsRunning() const { return m_running.load(std::memory_order_acquire); }
    // false only when the server claims to be running but one of the
    // two components that actually make it reachable has silently
    // stopped executing true whenever the server is not running at all
    // since nothing unexpected can be observed in that state
    bool IsHealthy() const;
    bool IsInitialScanComplete() const { return m_initialScanComplete.load(std::memory_order_acquire); }
    bool IsInitialScanInProgress() const { return m_initialScanInProgress.load(std::memory_order_acquire); }
    std::wstring GetEndpoint() const;
    std::vector<NetworkEndpoint> GetEndpoints() const;
    void RefreshEndpoints(const ConfigSnapshot& cfg);

private:
    Server();
    ~Server();
    bool ShouldStartScan() const;
    void StartBackgroundScan();
    void JoinBackgroundScan();
    void JoinBackgroundScanLocked();
    void StartWatchMode();
    void StopWatchMode();
    void WatchLoop();
    void StartNetworkChangeWatcher();
    void StopNetworkChangeWatcher();
    void StartDirectoryWatcher();
    void StopDirectoryWatcher();
    void TriggerAutoRescanIfEnabled();
    std::vector<std::wstring> LocalWatchFolders() const;

    std::atomic<bool> m_running;
    std::atomic<bool> m_stopping;
    // claimed for the duration of Start()'s body only released when
    // Start() returns by any path see the StartingGuard raii type
    // inside Start() itself this is not the same flag as m_running
    // m_running only becomes true once startup has fully succeeded
    std::atomic<bool> m_starting;
    std::atomic<bool> m_initialScanComplete;   // becomes: "root container exists"
    std::atomic<bool> m_initialScanInProgress; // NEW: true while the very first scan runs
    std::wstring m_endpoint;
    std::vector<NetworkEndpoint> m_endpoints;
    std::thread m_scanThread;
    std::thread m_scanCompletionThread;
    std::thread m_watchThread;
    std::mutex m_scanMutex;
    std::mutex m_rescanMutex;
    mutable std::mutex m_endpointMutex;
    std::mutex m_ssdpRestartMutex;
    std::chrono::steady_clock::time_point m_lastSsdpRestartCompletedAt{};
    bool m_hasSsdpRestartCompletedAt = false;
    std::mutex m_watchThreadMutex;
    std::mutex m_watchMutex;
    std::condition_variable m_watchCv;
    std::atomic<bool> m_stopWatch;
};

#define DLNAServer Server::Get()

#endif // SERVER_H
