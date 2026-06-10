#include "server.h"
#include "config.h"
#include "dlna_utils.h"
#include "log.h"
#include "media_sources.h"
#include "ssdp.h"
#include "httpserver.h"
#include "ipwhitelist.h"
#include "firewall_access.h"

#include <winsock2.h>
#include <ws2tcpip.h>

#pragma comment(lib, "ws2_32.lib")

Server& Server::Get() {
    static Server instance;
    return instance;
}

Server::Server() : m_running(false) {
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);
}

Server::~Server() {
    Stop();
    WSACleanup();
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
    if (!EnumerateNetworkEndpoints(cfg.port, m_endpoints)) {
        return;
    }

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

    AppConfig.Load();
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
        MessageBoxW(NULL, L"Please add at least one media source before starting the server.", L"No sources", MB_ICONWARNING | MB_OK);
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
    if (m_endpoints.empty()) {
        LogPrint(L"Failed to find any active network endpoint for discovery.");
        return false;
    }

    const NetworkEndpoint* displayEndpoint = NULL;
    for (const auto& endpoint : m_endpoints) {
        if (endpoint.family == AF_INET) {
            displayEndpoint = &endpoint;
            break;
        }
    }
    if (displayEndpoint == NULL) {
        displayEndpoint = &m_endpoints.front();
    }

    m_endpoint = std::wstring(displayEndpoint->host.begin(), displayEndpoint->host.end()) + L":" + std::to_wstring(cfg.port);
    LogPrint(L"Starting server on %ls", m_endpoint.c_str());

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
    
    LogPrint(L"Stopping server...");
    
    SSDP::Get().Stop();
    HttpServer::Get().Stop();
    JoinBackgroundScan();
    
    m_running = false;
    m_endpoint = L"";
}
