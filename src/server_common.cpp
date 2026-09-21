#include "server.h"
#include "config.h"
#include "scan_cancellation.h"
#include "contentdirectory.h"
#include "dlna_utils.h"
#include "log.h"
#include "media_sources.h"
#include "thread_guard.h"
#include "source_watcher.h"
#include "ssdp.h"
#include "httpserver.h"
#include "netchange_watch.h"
#include "dirwatch.h"
#include "network_sources.h"
#include "ipwhitelist.h"

#include <chrono>
#include <curl/curl.h>

Server& Server::Get() {
    static Server instance;
    return instance;
}

bool Server::ShouldStartScan() const {
    return m_running.load(std::memory_order_acquire) && !m_stopping.load(std::memory_order_acquire);
}

void Server::StartBackgroundScan() {
    if (!ShouldStartScan()) {
        return;
    }

    std::thread previousScan;
    {
        std::lock_guard<std::mutex> lock(m_scanMutex);
        if (m_scanThread.joinable()) {
            previousScan = std::move(m_scanThread);
        }
    }
    if (previousScan.joinable()) {
        previousScan.join();
    }
    if (!ShouldStartScan()) return;

    std::lock_guard<std::mutex> lock(m_scanMutex);
    if (m_scanThread.joinable()) {
        return;
    }
    m_scanThread = std::thread([]() { RunGuarded(L"media-scan", []() { AppMedia.Scan(); }); });
}

void Server::JoinBackgroundScan() {
    std::thread previousScan;
    {
        std::lock_guard<std::mutex> lock(m_scanMutex);
        if (m_scanThread.joinable()) {
            previousScan = std::move(m_scanThread);
        }
    }
    if (previousScan.joinable()) {
        previousScan.join();
    }
}

void Server::TriggerAutoRescanIfEnabled() {
    ConfigSnapshot cfg = AppConfig.Snapshot();
    if (ShouldAutoRescan(cfg, true)) {
        LogPrint(L"Media source change detected; rescanning.");
        if (!m_stopWatch.load(std::memory_order_acquire)) {
            StartBackgroundScan();
        }
    }
}

std::vector<std::wstring> Server::LocalWatchFolders() const {
    ConfigSnapshot cfg = AppConfig.Snapshot();
    std::vector<std::wstring> folders;
    for (const auto& source : cfg.effectiveMediaSources) {
        if (!IsRemoteMediaUrl(source.path)) {
            folders.push_back(source.path);
        }
    }
    return folders;
}

void Server::StartNetworkChangeWatcher() {
    StartNetworkChangeWatch([this]() {
        RestartSsdpForNetworkChange();
    });
}

void Server::RestartSsdpForNetworkChange() {
    std::lock_guard<std::mutex> lock(m_ssdpRestartMutex);
    if (!m_running.load(std::memory_order_acquire)) return;
    const auto now = std::chrono::steady_clock::now();
    if (m_hasSsdpRestartCompletedAt && (now - m_lastSsdpRestartCompletedAt) < std::chrono::seconds(2)) {
        LogPrint(L"SSDP restart skipped: already restarted less than 2s ago.");
        return;
    }
    LogPrint(L"Network topology change detected; refreshing endpoints");
    const std::vector<NetworkEndpoint> endpointsBeforeRefresh = GetEndpoints();
    const ConfigSnapshot cfg = AppConfig.Snapshot();
    RefreshEndpoints(cfg);
    const std::vector<NetworkEndpoint> endpointsAfterRefresh = GetEndpoints();
    if (NetworkEndpointSetsEqual(endpointsBeforeRefresh, endpointsAfterRefresh)) {
        LogPrint(L"Endpoint set unchanged after refresh; SSDP restart skipped");
        m_lastSsdpRestartCompletedAt = std::chrono::steady_clock::now();
        m_hasSsdpRestartCompletedAt = true;
        return;
    }
    InvalidateRoutableHostUrlCache();
    SSDP::Get().Stop();
    SSDP::Get().Start(endpointsAfterRefresh, cfg.port, cfg.serverName, cfg.deviceUUID);
    m_lastSsdpRestartCompletedAt = std::chrono::steady_clock::now();
    m_hasSsdpRestartCompletedAt = true;
}

void Server::StopNetworkChangeWatcher() {
    StopNetworkChangeWatch();
}

void Server::StartDirectoryWatcher() {
    m_directoryWatchActive.store(StartDirectoryWatch(LocalWatchFolders(), [this]() {
        if (!m_running.load(std::memory_order_acquire)) return;
        TriggerAutoRescanIfEnabled();
    }), std::memory_order_release);
}

void Server::StopDirectoryWatcher() {
    StopDirectoryWatch();
}

void Server::StartWatchMode() {
    StopWatchMode();
    m_stopWatch.store(false);
    std::lock_guard<std::mutex> lock(m_watchThreadMutex);
    m_watchThread = std::thread(&Server::WatchLoop, this);
}

void Server::StopWatchMode() {
    m_stopWatch.store(true);
    m_watchCv.notify_all();
    std::thread threadToJoin;
    {
        std::lock_guard<std::mutex> lock(m_watchThreadMutex);
        if (m_watchThread.joinable()) {
            threadToJoin = std::move(m_watchThread);
        }
    }
    if (threadToJoin.joinable()) {
        threadToJoin.join();
    }
}

void Server::WatchLoop() {
    ConfigSnapshot cfg = AppConfig.Snapshot();
    std::string signature = ComputeMediaSourceSignature(cfg);
    while (!m_stopWatch.load()) {
        std::unique_lock<std::mutex> lock(m_watchMutex);
        const auto pollInterval = m_directoryWatchActive.load(std::memory_order_acquire)
            ? std::chrono::minutes(15)
            : std::chrono::seconds(60);
        if (m_watchCv.wait_for(lock, pollInterval, [&]() { return m_stopWatch.load(); })) {
            break;
        }
        lock.unlock();

        cfg = AppConfig.Snapshot();
        const bool sourcesChanged = MediaSourcesHaveChanged(cfg, signature);
        if (ShouldAutoRescan(cfg, sourcesChanged)) {
            LogPrint(L"Media source change detected; rescanning.");
            if (!m_stopWatch.load(std::memory_order_acquire)) {
                StartBackgroundScan();
            }
        }
    }
}

void Server::RefreshEndpoints(const ConfigSnapshot& cfg) {
    std::vector<NetworkEndpoint> endpoints;
    if (!EnumerateNetworkEndpoints(cfg.port, cfg.networkInterfaceAllowList, endpoints)) {
        LogPrint(L"Network endpoint enumeration failed.");
        std::lock_guard<std::mutex> lock(m_endpointMutex);
        m_endpoint = L"";
        m_endpoints.clear();
        return;
    }

    if (cfg.debugLog) {
        for (const auto& endpoint : endpoints) {
            LogPrint(L"Discovery endpoint selected: family=%d addr=%hs if=%lu prefix=%lu location=%hs",
                     endpoint.family,
                     endpoint.address.c_str(),
                     endpoint.interfaceIndex,
                     endpoint.prefixLength,
                     endpoint.locationUrl.c_str());
        }
    }

    std::lock_guard<std::mutex> lock(m_endpointMutex);
    m_endpoints = std::move(endpoints);
}

// true when not running since nothing can be unexpectedly wrong
// while running both reachable components must still be alive
bool Server::IsHealthy() const {
    if (!m_running.load(std::memory_order_acquire)) return true;
    return HttpServer::Get().IsHealthy() && SSDP::Get().IsHealthy();
}

// snapshot copy under the endpoint mutex for hook and start callers
std::vector<NetworkEndpoint> Server::GetEndpoints() const {
    std::lock_guard<std::mutex> lock(m_endpointMutex);
    return m_endpoints;
}

bool Server::Rescan() {
    std::lock_guard<std::mutex> rescanLock(m_rescanMutex);
    AppScanCancel.BeginScan();
    AppMedia.ResetForRescan();
    AppContent.ClearSearchCache();
    if (m_running.load(std::memory_order_acquire)) {
        StartBackgroundScan();
        JoinBackgroundScan();
    } else {
        AppMedia.Scan();
    }
    StopDirectoryWatcher();
    StartDirectoryWatcher();
    return true;
}

void Server::Stop() {
    if (!m_running.exchange(false, std::memory_order_acq_rel)) return;
    m_stopping.store(true, std::memory_order_release);
    AppScanCancel.RequestCancel();

    LogPrint(L"Stopping server");

    if (m_scanCompletionThread.joinable()) {
        m_scanCompletionThread.join();
    }

    StopWatchMode();
    StopNetworkChangeWatcher();
    StopDirectoryWatcher();
    SSDP::Get().Stop();
    HttpServer::Get().Stop();
    JoinBackgroundScan();

    AppConfig.ClearRuntimeSourceOverride();

    {
        std::lock_guard<std::mutex> lock(m_endpointMutex);
        m_endpoint = L"";
        m_endpoints.clear();
    }
}
