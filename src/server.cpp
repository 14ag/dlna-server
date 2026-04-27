#include "server.h"
#include "config.h"
#include "log.h"
#include "media_sources.h"
#include "ssdp.h"
#include "httpserver.h"
#include "ipwhitelist.h"

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

void Server::RefreshEndpoints() {
    m_endpoints.clear();
    if (!EnumerateNetworkEndpoints(AppConfig.port, m_endpoints)) {
        return;
    }

    if (AppConfig.debugLog) {
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
    IPWhitelist::Get().Load(AppConfig.ipWhiteList);

    // Validate we have at least one source
    bool hasSource = false;
    for (const auto& s : AppConfig.mediaSources) {
        if (s.enabled) { hasSource = true; break; }
    }
    if (!hasSource) {
        MessageBoxW(NULL, L"Please add at least one shared folder before starting the server.", L"No sources", MB_ICONWARNING | MB_OK);
        return false;
    }

    AppMedia.Scan();
    RefreshEndpoints();
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

    m_endpoint = std::wstring(displayEndpoint->host.begin(), displayEndpoint->host.end()) + L":" + std::to_wstring(AppConfig.port);
    LogPrint(L"Starting server on %ls", m_endpoint.c_str());

    if (!HttpServer::Get().Start(AppConfig.port)) {
        LogPrint(L"Failed to start HTTP server.");
        return false;
    }

    if (!SSDP::Get().Start(m_endpoints, AppConfig.port, AppConfig.serverName, AppConfig.deviceUUID)) {
        LogPrint(L"Failed to start SSDP.");
        HttpServer::Get().Stop();
        return false;
    }

    m_running = true;
    return true;
}

void Server::Stop() {
    if (!m_running) return;
    
    LogPrint(L"Stopping server...");
    
    SSDP::Get().Stop();
    HttpServer::Get().Stop();
    
    m_running = false;
    m_endpoint = L"";
}
