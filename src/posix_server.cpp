#include "server.h"
#include "config.h"
#include "contentdirectory.h"
#include "dlna_utils.h"
#include "httpserver.h"
#include "ipwhitelist.h"
#include "log.h"
#include "media_sources.h"
#include "source_watcher.h"
#include "ssdp.h"

#include <chrono>

Server& Server::Get() {
    static Server instance;
    return instance;
}

Server::Server() : m_running(false), m_stopping(false), m_stopWatch(false), m_initialScanComplete(false), m_initialScanInProgress(false) {
}

Server::~Server() {
    Stop();
}

std::wstring Server::GetEndpoint() const {
    std::lock_guard<std::mutex> lock(m_endpointMutex);
    return m_endpoint;
}

std::vector<NetworkEndpoint> Server::GetEndpoints() const {
    std::lock_guard<std::mutex> lock(m_endpointMutex);
    return m_endpoints;
}

bool Server::ShouldStartScan() const {
    return m_running.load(std::memory_order_acquire) && !m_stopping.load(std::memory_order_acquire);
}

void Server::StartBackgroundScan() {
    if (!ShouldStartScan()) return;

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
    if (m_scanThread.joinable()) return;
    m_scanThread = std::thread([]() { AppMedia.Scan(); });
}

void Server::JoinBackgroundScanLocked() {
    if (m_scanThread.joinable()) {
        m_scanThread.join();
    }
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
        if (m_watchCv.wait_for(lock, std::chrono::seconds(5), [&]() { return m_stopWatch.load(); })) {
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
        m_endpoint.clear();
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

bool Server::Start(std::wstring& outReason) {
    if (m_running.load(std::memory_order_acquire)) return true;
    m_stopping.store(false, std::memory_order_release);
    const ConfigSnapshot cfg = AppConfig.Snapshot();
    IPWhitelist::Get().Load(cfg.ipWhiteList);
    if (!IsValidPort(cfg.port)) {
        LogPrint(L"Invalid HTTP port: %d", cfg.port);
        outReason = L"Invalid port: " + std::to_wstring(cfg.port);
        return false;
    }
    bool hasSource = !cfg.hasRuntimeSourceOverride &&
                      cfg.defaultPlaylistEnabled && !cfg.defaultPlaylistPath.empty();
    if (!cfg.effectiveMediaSources.empty()) hasSource = true;
    if (!hasSource) {
        LogPrint(L"No media sources configured; refusing to serve current directory.");
        outReason = L"No media sources configured";
        return false;
    }
    RefreshEndpoints(cfg);
    std::vector<NetworkEndpoint> endpoints = GetEndpoints();
    if (endpoints.empty()) {
        LogPrint(L"Failed to find any active network endpoint for discovery.");
        outReason = L"No active network endpoints found";
        return false;
    }
    const NetworkEndpoint* displayEndpoint = SelectBestEndpoint(endpoints, nullptr);
    const std::wstring endpointText = Utf8ToWide(displayEndpoint->host + ":" + std::to_string(cfg.port));
    {
        std::lock_guard<std::mutex> lock(m_endpointMutex);
        m_endpoint = endpointText;
    }
    // Initialize the content directory before starting the HTTP/SSDP layers
    // so the root container exists and the scan-in-progress flag is set
    // before any client can connect and issue a Browse/Search request.
    // This eliminates the window where a Browse arriving immediately after
    // the port opens would see m_initialScanComplete==false and return 710.
    AppMedia.ResetForRescan();
    m_initialScanComplete.store(true, std::memory_order_release);
    m_initialScanInProgress.store(true, std::memory_order_release);

    if (!HttpServer::Get().Start(cfg.port)) {
        LogPrint(L"Failed to start HTTP server.");
        outReason = L"Failed to start HTTP server on port " + std::to_wstring(cfg.port);
        return false;
    }
    if (!SSDP::Get().Start(m_endpoints, cfg.port, cfg.serverName, cfg.deviceUUID)) {
        LogPrint(L"Failed to start SSDP.");
        outReason = L"Failed to start SSDP discovery";
        HttpServer::Get().Stop();
        return false;
    }
    m_running.store(true, std::memory_order_release);
    StartBackgroundScan();
    // Do not JoinBackgroundScan() here: Start() must return once the device is
    // advertised, not once the library is fully indexed. The scan continues on
    // m_scanThread; StartWatchMode() begins after Start() returns via a small
    // completion hook below. This hook thread is stored in
    // m_scanCompletionThread and joined at the top of Stop(), never
    // detached, so it can never outlive this singleton.
    if (m_scanCompletionThread.joinable()) {
        m_scanCompletionThread.join();
    }
    m_scanCompletionThread = std::thread([this]() {
        JoinBackgroundScan();
        m_initialScanInProgress.store(false, std::memory_order_release);
        StartWatchMode();
    });
    LogPrint(L"DLNA server running on %ls", endpointText.c_str());
    return true;
}

bool Server::Rescan() {
    // serialize the whole reset then scan sequence per caller
    // see src/server.cpp Server::Rescan for the full rationale
    std::lock_guard<std::mutex> rescanLock(m_rescanMutex);
    AppMedia.ResetForRescan();
    if (m_running.load(std::memory_order_acquire)) {
        StartBackgroundScan();
        JoinBackgroundScan();
    } else {
        AppMedia.Scan();
    }
    return true;
}

void Server::Stop() {
    if (!m_running.exchange(false, std::memory_order_acq_rel)) return;
    m_stopping.store(true, std::memory_order_release);
    if (m_scanCompletionThread.joinable()) {
        m_scanCompletionThread.join();
    }
    StopWatchMode();
    SSDP::Get().Stop();
    HttpServer::Get().Stop();
    JoinBackgroundScan();

    AppConfig.ClearRuntimeSourceOverride();

    {
        std::lock_guard<std::mutex> lock(m_endpointMutex);
        m_endpoint.clear();
        m_endpoints.clear();
    }
}
