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
#include "firewall_access.h"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <chrono>
#include <curl/curl.h>

#pragma comment(lib, "ws2_32.lib")

namespace {
bool IsSkipFirewallEnvVarPresent() {
    wchar_t buffer[8] = {};
    DWORD copied = GetEnvironmentVariableW(L"DLNA_SERVER_SKIP_FIREWALL", buffer, 8);
    if (copied != 0) return true;
    return GetLastError() != ERROR_ENVVAR_NOT_FOUND;
}
}

Server::Server() : m_running(false), m_stopping(false), m_starting(false), m_initialScanComplete(false), m_initialScanInProgress(false), m_stopWatch(false) {
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);
    curl_global_init(CURL_GLOBAL_DEFAULT);
}

Server::~Server() {
    Stop();
    curl_global_cleanup();
    WSACleanup();
}

bool Server::Start(std::wstring& outReason) {
    if (m_running.load(std::memory_order_acquire)) return true;
    bool expectedNotStarting = false;
    if (!m_starting.compare_exchange_strong(expectedNotStarting, true, std::memory_order_acq_rel)) {
        outReason = L"Server is already starting on another thread";
        return false;
    }
    struct StartingGuard {
        std::atomic<bool>& flag;
        ~StartingGuard() { flag.store(false, std::memory_order_release); }
    } startingGuard{ m_starting };
    m_stopping.store(false, std::memory_order_release);
    AppScanCancel.BeginScan();

    const ConfigSnapshot cfg = AppConfig.Snapshot();
    IPWhitelist::Get().Load(cfg.ipWhiteList);
    if (!IsValidPort(cfg.port)) {
        LogPrint(L"Invalid HTTP port: %d", cfg.port);
        outReason = L"Invalid port: " + std::to_wstring(cfg.port);
        return false;
    }
    // Validate we have at least one source
    bool hasSource = !cfg.hasRuntimeSourceOverride &&
                      cfg.defaultPlaylistEnabled && !cfg.defaultPlaylistPath.empty();
    if (!cfg.effectiveMediaSources.empty()) hasSource = true;
    if (!hasSource) {
        LogPrint(L"No media sources configured.");
        outReason = L"No media sources configured";
        return false;
    }

    if (!IsSkipFirewallEnvVarPresent()) {
        std::wstring firewallMessage;
        if (!EnsureFirewallAccess(cfg.port, FirewallAccessMode::Interactive, firewallMessage)) {
            LogPrint(L"%ls", firewallMessage.c_str());
            outReason = L"Firewall access was denied";
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
        outReason = L"No active network endpoints found";
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
    //
    // m_rescanMutex is held from here through the end of this function via
    // RAII (released on every return path below, including the HTTP/SSDP
    // failure paths). Server::Rescan() takes the same mutex around its own
    // ResetForRescan()+scan sequence. Without this lock, a scan triggered
    // by adding a media source (MainWindow::AddMediaSourceIfNew ->
    // Server::Rescan(), fired on a detached thread with no UI busy-gate)
    // can still be publishing MediaItems when Start() runs on a second
    // thread moments later, and Start()'s ResetForRescan() call wipes the
    // catalog out from under it. See F-CRASH-01.
    {
        std::lock_guard<std::mutex> startScanLock(m_rescanMutex);
        AppMedia.ResetForRescan();
        AppContent.ClearSearchCache();
        m_initialScanComplete.store(true, std::memory_order_release);
        m_initialScanInProgress.store(true, std::memory_order_release);
    }

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
        RunGuarded(L"scan-completion", [this]() {
            JoinBackgroundScan();
            m_initialScanInProgress.store(false, std::memory_order_release);
            StartWatchMode();
        });
    });
    StartNetworkChangeWatcher();
    StartDirectoryWatcher();
    return true;
}

