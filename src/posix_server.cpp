#include "server.h"
#include "config.h"
#include "scan_cancellation.h"
#include "contentdirectory.h"
#include "dlna_utils.h"
#include "httpserver.h"
#include "ipwhitelist.h"
#include "log.h"
#include "media_sources.h"
#include "thread_guard.h"
#include "network_sources.h"
#include "source_watcher.h"
#include "ssdp.h"
#include "netchange_watch.h"
#include "dirwatch.h"

#include <chrono>
#include <curl/curl.h>

Server::Server() : m_running(false), m_stopping(false), m_starting(false), m_initialScanComplete(false), m_initialScanInProgress(false), m_stopWatch(false) {
    curl_global_init(CURL_GLOBAL_DEFAULT);
}

Server::~Server() {
    Stop();
    curl_global_cleanup();
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
    LogPrint(L"DLNA server running on %ls", endpointText.c_str());
    return true;
}
