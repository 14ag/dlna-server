#include "server.h"
#include "config.h"
#include "contentdirectory.h"
#include "dlna_utils.h"
#include "httpserver.h"
#include "ipwhitelist.h"
#include "log.h"
#include "media_sources.h"
#include "ssdp.h"

Server& Server::Get() {
    static Server instance;
    return instance;
}

Server::Server() : m_running(false) {
}

Server::~Server() {
    Stop();
}

void Server::StartBackgroundScan() {
    JoinBackgroundScan();
    m_scanThread = std::thread([]() {
        AppMedia.Scan();
    });
}

void Server::JoinBackgroundScan() {
    if (m_scanThread.joinable()) {
        m_scanThread.join();
    }
}

void Server::RefreshEndpoints(const ConfigSnapshot& cfg) {
    m_endpoints.clear();
    EnumerateNetworkEndpoints(cfg.port, m_endpoints);
    if (cfg.debugLog) {
        for (const auto& endpoint : m_endpoints) {
            LogPrint(L"Discovery endpoint selected: family=%d addr=%hs if=%lu prefix=%lu location=%hs",
                     endpoint.family,
                     endpoint.address.c_str(),
                     endpoint.interfaceIndex,
                     endpoint.prefixLength,
                     endpoint.locationUrl.c_str());
        }
    }
}

bool Server::Start() {
    if (m_running) return true;
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
    if (m_endpoints.empty()) {
        LogPrint(L"Failed to find any active network endpoint for discovery.");
        return false;
    }
    const NetworkEndpoint* displayEndpoint = SelectBestEndpoint(m_endpoints, nullptr);
    m_endpoint = Utf8ToWide(displayEndpoint->host + ":" + std::to_string(cfg.port));
    if (!HttpServer::Get().Start(cfg.port)) {
        LogPrint(L"Failed to start HTTP server.");
        return false;
    }
    if (!SSDP::Get().Start(m_endpoints, cfg.port, cfg.serverName, cfg.deviceUUID)) {
        LogPrint(L"Failed to start SSDP.");
        HttpServer::Get().Stop();
        return false;
    }
    m_running = true;
    StartBackgroundScan();
    LogPrint(L"DLNA server running on %ls", m_endpoint.c_str());
    return true;
}

bool Server::Rescan() {
    if (m_running) {
        StartBackgroundScan();
    } else {
        AppMedia.Scan();
    }
    return true;
}

void Server::Stop() {
    if (!m_running) return;
    SSDP::Get().Stop();
    HttpServer::Get().Stop();
    JoinBackgroundScan();
    m_running = false;
}
