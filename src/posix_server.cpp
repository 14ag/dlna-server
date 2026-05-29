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

void Server::RefreshEndpoints() {
    m_endpoints.clear();
    EnumerateNetworkEndpoints(AppConfig.port, m_endpoints);
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
    IPWhitelist::Get().Load(AppConfig.ipWhiteList);
    if (!IsValidPort(AppConfig.port)) {
        LogPrint(L"Invalid HTTP port: %d", AppConfig.port);
        return false;
    }
    if (AppConfig.mediaSources.empty() && !AppConfig.defaultPlaylistEnabled) {
        AppConfig.mediaSources.push_back({L".", true});
    }
    AppMedia.Scan();
    RefreshEndpoints();
    if (m_endpoints.empty()) {
        LogPrint(L"Failed to find any active network endpoint for discovery.");
        return false;
    }
    const NetworkEndpoint* displayEndpoint = SelectBestEndpoint(m_endpoints, nullptr);
    m_endpoint = Utf8ToWide(displayEndpoint->host + ":" + std::to_string(AppConfig.port));
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
    LogPrint(L"DLNA server running on %ls", m_endpoint.c_str());
    return true;
}

void Server::Stop() {
    if (!m_running) return;
    SSDP::Get().Stop();
    HttpServer::Get().Stop();
    m_running = false;
}
