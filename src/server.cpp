#include "server.h"
#include "config.h"
#include "log.h"
#include "media_sources.h"
#include "ssdp.h"
#include "httpserver.h"
#include "ipwhitelist.h"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "iphlpapi.lib")

Server& Server::Get() {
    static Server instance;
    return instance;
}

Server::Server() : m_running(false) {
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);
}

std::string Server::GetLocalIPv4() {
    std::string result = "127.0.0.1";
    ULONG outBufLen = 15000;
    PIP_ADAPTER_ADDRESSES pAddresses = (IP_ADAPTER_ADDRESSES*)malloc(outBufLen);
    
    if (GetAdaptersAddresses(AF_INET, GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER, NULL, pAddresses, &outBufLen) == NO_ERROR) {
        PIP_ADAPTER_ADDRESSES pCurrAddresses = pAddresses;
        while (pCurrAddresses) {
            if (pCurrAddresses->IfType != IF_TYPE_SOFTWARE_LOOPBACK && pCurrAddresses->OperStatus == IfOperStatusUp) {
                PIP_ADAPTER_UNICAST_ADDRESS pUnicast = pCurrAddresses->FirstUnicastAddress;
                if (pUnicast) {
                    sockaddr_in* sa_in = (sockaddr_in*)pUnicast->Address.lpSockaddr;
                    char ipBuf[INET_ADDRSTRLEN];
                    inet_ntop(AF_INET, &(sa_in->sin_addr), ipBuf, INET_ADDRSTRLEN);
                    std::string ipStr(ipBuf);
                    if (ipStr.find("169.254.") != 0) { // Skip APIPA
                        result = ipStr;
                        break;
                    }
                }
            }
            pCurrAddresses = pCurrAddresses->Next;
        }
    }
    free(pAddresses);
    return result;
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

    std::string ipStr = GetLocalIPv4();
    m_endpoint = std::wstring(ipStr.begin(), ipStr.end()) + L":" + std::to_wstring(AppConfig.port);

    LogPrint(L"Starting server on %s", m_endpoint.c_str());

    if (!HttpServer::Get().Start(AppConfig.port)) {
        LogPrint(L"Failed to start HTTP server.");
        return false;
    }

    if (!SSDP::Get().Start(ipStr, AppConfig.port, AppConfig.serverName, AppConfig.deviceUUID)) {
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
