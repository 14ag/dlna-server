#include "server.h"
#include "config.h"
#include "dlna_utils.h"
#include "log.h"
#include "media_sources.h"
#include "source_watcher.h"
#include "ssdp.h"
#include "httpserver.h"
#include "ipwhitelist.h"
#include "firewall_access.h"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <chrono>

#pragma comment(lib, "ws2_32.lib")

Server& Server::Get() {
    static Server instance;
    return instance;
}

Server::Server() : m_running(false), m_stopping(false), m_stopWatch(false), m_initialScanComplete(false), m_initialScanInProgress(false) {
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);
}

Server::~Server() {
    Stop();
    WSACleanup();
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
        LogPrint(L"[diag] StartBackgroundScan: scan already running, skipping");
        return;
    }
    LogPrint(L"[diag] StartBackgroundScan: creating scan thread");
    m_scanThread = std::thread([]() { AppMedia.Scan(); });
    LogPrint(L"[diag] StartBackgroundScan: thread created");
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
    m_watchThread = std::thread(&Server::WatchLoop, this);
}

void Server::StopWatchMode() {
    m_stopWatch.store(true);
    m_watchCv.notify_all();
    if (m_watchThread.joinable()) {
        m_watchThread.join();
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
    if (!EnumerateNetworkEndpoints(cfg.port, endpoints)) {
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

bool Server::Start() {
    if (m_running.load(std::memory_order_acquire)) return true;
    m_stopping.store(false, std::memory_order_release);

    const ConfigSnapshot cfg = AppConfig.Snapshot();
    IPWhitelist::Get().Load(cfg.ipWhiteList);
    if (!IsValidPort(cfg.port)) {
        LogPrint(L"Invalid HTTP port: %d", cfg.port);
        return false;
    }
    if (cfg.fileServerPort != cfg.port) {
        LogPrint(L"FileServerPort is deprecated; serving media on Port %d.", cfg.port);
    }

    // Validate we have at least one source
    bool hasSource = cfg.defaultPlaylistEnabled && !cfg.defaultPlaylistPath.empty();
    for (const auto& s : cfg.mediaSources) {
        if (s.enabled) { hasSource = true; break; }
    }
    if (!hasSource) {
        LogPrint(L"No media sources configured.");
        return false;
    }

    wchar_t skipFirewall[8] = {};
    if (GetEnvironmentVariableW(L"DLNA_SERVER_SKIP_FIREWALL", skipFirewall, 8) == 0) {
        std::wstring firewallMessage;
        if (!EnsureFirewallAccess(cfg.port, FirewallAccessMode::Interactive, firewallMessage)) {
            LogPrint(L"%ls", firewallMessage.c_str());
            MessageBoxW(NULL, firewallMessage.c_str(), L"Firewall access required", MB_ICONWARNING | MB_OK);
            return false;
        }
        if (!firewallMessage.empty()) {
            LogPrint(L"%ls", firewallMessage.c_str());
        }
    }

    RefreshEndpoints(cfg);
    std::vector<NetworkEndpoint> endpoints = GetEndpoints();
    if (endpoints.empty()) {
        LogPrint(L"Failed to find any active network endpoint for discovery.");
        return false;
    }

    const NetworkEndpoint* displayEndpoint = NULL;
    for (const auto& endpoint : endpoints) {
        if (endpoint.family == AF_INET) {
            displayEndpoint = &endpoint;
            break;
        }
    }
    if (displayEndpoint == NULL) {
        displayEndpoint = &endpoints.front();
    }

    const std::wstring endpointText = std::wstring(displayEndpoint->host.begin(), displayEndpoint->host.end()) + L":" + std::to_wstring(cfg.port);
    {
        std::lock_guard<std::mutex> lock(m_endpointMutex);
        m_endpoint = endpointText;
    }
    LogPrint(L"Starting server on %ls", endpointText.c_str());

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
        return false;
    }

    if (!SSDP::Get().Start(m_endpoints, cfg.port, cfg.serverName, cfg.deviceUUID)) {
        LogPrint(L"Failed to start SSDP.");
        HttpServer::Get().Stop();
        return false;
    }
    LogPrint(L"[diag:server] After SSDP::Get().Start returned - about to store m_running");

    m_running.store(true, std::memory_order_release);
    LogPrint(L"[diag:server] After m_running.store - about to call StartBackgroundScan");
    StartBackgroundScan();
    // Do not JoinBackgroundScan() here: Start() must return once the device is
    // advertised, not once the library is fully indexed. The scan continues on
    // m_scanThread; StartWatchMode() begins after Start() returns via a small
    // completion hook below.
    std::thread([this]() {
        JoinBackgroundScan();
        m_initialScanInProgress.store(false, std::memory_order_release);
        StartWatchMode();
    }).detach();
    return true;
}

bool Server::Rescan() {
    AppMedia.ResetForRescan();
    if (m_running.load(std::memory_order_acquire)) {
        // StartBackgroundScan only launches the scan thread and returns
        // Rescan must not report completion until the scan is done
        // JoinBackgroundScan blocks this call until that thread finishes
        // every caller of Rescan already runs on its own worker thread
        // see MainWindow::BeginRescan so this never blocks the UI thread
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
    
    LogPrint(L"Stopping server");

    StopWatchMode();
    SSDP::Get().Stop();
    HttpServer::Get().Stop();
    JoinBackgroundScan();
    
    {
        std::lock_guard<std::mutex> lock(m_endpointMutex);
        m_endpoint = L"";
        m_endpoints.clear();
    }
}
