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

Server::Server() : m_running(false), m_stopping(false), m_stopWatch(false) {
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
        if (MediaSourcesHaveChanged(cfg, signature)) {
            LogPrint(L"Media source change detected; rescanning.");
            if (!m_stopWatch.load(std::memory_order_acquire)) {
                StartBackgroundScan();
            }
        }
    }
}

void Server::RefreshEndpoints(const ConfigSnapshot& cfg) {
    std::vector<NetworkEndpoint> endpoints;
    EnumerateNetworkEndpoints(cfg.port, endpoints);
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
    bool hasSource = cfg.defaultPlaylistEnabled && !cfg.defaultPlaylistPath.empty();
    for (const auto& source : cfg.mediaSources) {
        if (source.enabled) {
            hasSource = true;
            break;
        }
    }
    if (!hasSource) {
        LogPrint(L"No media sources configured; refusing to serve current directory.");
        return false;
    }
    RefreshEndpoints(cfg);
    std::vector<NetworkEndpoint> endpoints = GetEndpoints();
    if (endpoints.empty()) {
        LogPrint(L"Failed to find any active network endpoint for discovery.");
        return false;
    }
    const NetworkEndpoint* displayEndpoint = SelectBestEndpoint(endpoints, nullptr);
    const std::wstring endpointText = Utf8ToWide(displayEndpoint->host + ":" + std::to_string(cfg.port));
    {
        std::lock_guard<std::mutex> lock(m_endpointMutex);
        m_endpoint = endpointText;
    }
    if (!HttpServer::Get().Start(cfg.port)) {
        LogPrint(L"Failed to start HTTP server.");
        return false;
    }
    if (!SSDP::Get().Start(m_endpoints, cfg.port, cfg.serverName, cfg.deviceUUID)) {
        LogPrint(L"Failed to start SSDP.");
        HttpServer::Get().Stop();
        return false;
    }
    m_running.store(true, std::memory_order_release);
    StartBackgroundScan();
    StartWatchMode();
    LogPrint(L"DLNA server running on %ls", endpointText.c_str());
    return true;
}

bool Server::Rescan() {
    if (m_running.load(std::memory_order_acquire)) {
        StartBackgroundScan();
    } else {
        AppMedia.Scan();
    }
    return true;
}

void Server::Stop() {
    if (!m_running.exchange(false, std::memory_order_acq_rel)) return;
    m_stopping.store(true, std::memory_order_release);
    StopWatchMode();
    SSDP::Get().Stop();
    HttpServer::Get().Stop();
    JoinBackgroundScan();
    {
        std::lock_guard<std::mutex> lock(m_endpointMutex);
        m_endpoint.clear();
        m_endpoints.clear();
    }
}
