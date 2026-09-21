#include <windows.h>
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <fstream>
#include <future>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <thread>
#include "mainwindow.h"
#include "config.h"
#include "network_interface_policy.h"
#include "dlna_utils.h"
#include "http_common.h"
#include "httpserver.h"
#include "media_scan_common.h"
#include "firewall_access.h"
#include "log.h"
#include "netutils.h"
#include "network_sources.h"
#include "server.h"
#include "media_sources.h"
#include "media_database.h"
#include "browse_page_cap.h"
#include "thread_guard.h"
#include "settings_restart.h"
#include "source_drop_target.h"
#include "startup_mode.h"
#include "access_key_hook.h"
#include "access_keys.h"
#include "hover_focus_state.h"
#include "input_gate.h"
#include "playlist_scan_concurrency.h"
#include "scan_cancellation.h"
#include "transmitfile_chunking.h"
#include "cli_flags.h"
#include "media_source_file_types.h"
#include "contentdirectory.h"
#include "ssdp_common.h"
#include "ssdp.h"
#include "upnp_eventing.h"
#include "server_close_policy.h"
#include "health_check_policy.h"
#include "win_geometry_dump.h"
#include "close_pending_state.h"
#include "modal_stack.h"
#include "function_key_action.h"
#include "tray_notify.h"
#include "../resources/resource.h"

namespace {

bool IsPrintHook(const wchar_t* arg) {
    return arg && arg[0] == L'-' && arg[1] == L'-' && wcsstr(arg, L"--print-") == arg;
}

} // namespace

bool TryRunPrintHook(int argc, wchar_t** argv, int& exitCode) {
    if (argc < 2) return false;

    // Prescan argv in order and apply the same immediate side effects the
    // wWinMain loop applies before a print flag so that source override
    // debug flag and geometry flag state match the old inline behavior
    int i = 1;
    for (; i < argc; ++i) {
        if (wcscmp(argv[i], L"--source") == 0 && i + 1 < argc) {
            ++i;
            std::vector<std::wstring> parsedSources = ParseQuotedCommaList(argv[i]);
            std::vector<MediaSource> immediateOverride;
            if (parsedSources.empty()) {
                immediateOverride.push_back({argv[i]});
            } else {
                for (auto& parsed : parsedSources) {
                    immediateOverride.push_back({parsed});
                }
            }
            AppConfig.SetRuntimeSourceOverride(immediateOverride);
        } else if (wcscmp(argv[i], L"--no-debug") == 0) {
            AppConfig.debugLog = false;
        } else if (wcscmp(argv[i], L"--dump-widget-geometry") == 0) {
            DumpWidgetGeometryFlag() = true;
        } else if (IsPrintHook(argv[i])) {
            break;
        }
    }
    if (i >= argc) return false;
    if (wcscmp(argv[i], L"--print-should-dump-dialog-geometry") == 0 && i + 1 < argc) {
        bool flagEnabled = wcscmp(argv[++i], L"1") == 0;
        std::wcout << (ShouldDumpDialogGeometry(flagEnabled) ? L"1" : L"0") << std::endl;
        exitCode = 0;
        return true;
    } else if (wcscmp(argv[i], L"--print-scan-cancellation-lifecycle") == 0) {
        AppScanCancel.BeginScan();
        std::wcout << (AppScanCancel.IsCancelled() ? L"1" : L"0") << std::endl;
        AppScanCancel.RequestCancel();
        std::wcout << (AppScanCancel.IsCancelled() ? L"1" : L"0") << std::endl;
        AppScanCancel.BeginScan();
        std::wcout << (AppScanCancel.IsCancelled() ? L"1" : L"0") << std::endl;
        exitCode = 0;
        return true;
    } else if (wcscmp(argv[i], L"--print-single-instance-lifecycle") == 0) {
        HANDLE hMutex = CreateMutexW(NULL, TRUE, L"dlna-server_SingleInstance_Mutex");
        if (!hMutex) {
            exitCode = 1;
            return true;
        }
        bool acquired = GetLastError() != ERROR_ALREADY_EXISTS;
        if (!acquired) {
            DWORD waitResult = WaitForSingleObject(hMutex, 0);
            acquired = (waitResult == WAIT_OBJECT_0 || waitResult == WAIT_ABANDONED_0);
        }
        std::wcout << (acquired ? L"lock-acquired" : L"lock-busy") << std::endl;
        if (acquired) {
            ReleaseMutex(hMutex);
            std::wcout << L"released" << std::endl;
        }
        CloseHandle(hMutex);
        exitCode = 0;
        return true;
    } else if (wcscmp(argv[i], L"--print-log-since-lifecycle") == 0) {
        LogPrint(L"line-one");
        LogPrint(L"line-two");
        LogSnapshot first = GetSystemLogSince(0);
        std::wcout << L"first-latest=" << first.latestSequence << std::endl;
        LogPrint(L"line-three");
        LogSnapshot second = GetSystemLogSince(first.latestSequence);
        std::wcout << L"second-latest=" << second.latestSequence << std::endl;
        std::wcout << (second.text.find(L"line-three") != std::wstring::npos
                                ? L"has-new-line" : L"missing-new-line") << std::endl;
        std::wcout << (second.text.find(L"line-one") != std::wstring::npos
                                ? L"leaked-old-line" : L"no-old-line-leak") << std::endl;
        exitCode = 0;
        return true;
    } else if (wcscmp(argv[i], L"--print-scan-concurrency") == 0 && i + 1 < argc) {
        size_t n = static_cast<size_t>(_wtoi(argv[++i]));
        std::cout << ComputePlaylistScanConcurrency(n) << std::endl;
        exitCode = 0;
        return true;
    } else if (wcscmp(argv[i], L"--print-mnemonics") == 0 && i + 1 < argc) {
        std::wstring arg = argv[++i];
        std::vector<std::wstring> labels;
        size_t start = 0;
        for (size_t j = 0; j <= arg.size(); ++j) {
            if (j == arg.size() || arg[j] == L',') {
                labels.push_back(arg.substr(start, j - start));
                start = j + 1;
            }
        }
        std::vector<wchar_t> result = AssignMnemonics(labels);
        for (size_t j = 0; j < result.size(); ++j) {
            if (j > 0) std::cout << ",";
            if (result[j] != L'\0') {
                std::cout << static_cast<char>(result[j]);
            }
        }
        std::cout << std::endl;
        exitCode = 0;
        return true;
    } else if (wcscmp(argv[i], L"--print-cue-state") == 0 && i + 1 < argc) {
        std::wstring seq = argv[++i];
        KeyboardCueState cs;
        for (wchar_t ch : seq) {
            if (ch == L'k' || ch == L'K') cs.OnKeyboardInput();
            else if (ch == L'm' || ch == L'M') cs.OnMouseButtonInput();
            std::cout << (cs.HideAccel() ? "1" : "0") << "," << (cs.HideFocus() ? "1" : "0") << std::endl;
        }
        exitCode = 0;
        return true;
    } else if (wcscmp(argv[i], L"--print-hover-focus-state") == 0 && i + 1 < argc) {
        std::wstring seq = argv[++i];
        HoverFocusState state;
        size_t start = 0;
        while (start <= seq.size()) {
            size_t comma = seq.find(L',', start);
            std::wstring token = seq.substr(start, comma == std::wstring::npos ? std::wstring::npos : comma - start);
            if (!token.empty()) {
                wchar_t code = token[0];
                int id = _wtoi(token.c_str() + 1);
                if (code == L'e') state.OnMouseEnter(id);
                else if (code == L'l') state.OnMouseLeave(id);
                else if (code == L'f') state.OnFocusGained(id);
                else if (code == L'b') state.OnFocusLost(id);
                std::wcout << state.HighlightedControlId() << std::endl;
            }
            if (comma == std::wstring::npos) break;
            start = comma + 1;
        }
        exitCode = 0;
        return true;
    } else if (wcscmp(argv[i], L"--print-any-field-has-content") == 0 && i + 1 < argc) {
        std::wstring csv = argv[++i];
        std::vector<int> lens;
        size_t start = 0;
        while (start <= csv.size()) {
            size_t comma = csv.find(L',', start);
            std::wstring token = csv.substr(start, comma == std::wstring::npos ? std::wstring::npos : comma - start);
            if (!token.empty()) lens.push_back(_wtoi(token.c_str()));
            if (comma == std::wstring::npos) break;
            start = comma + 1;
        }
        std::wcout << (AnyFieldHasContent(lens) ? L"1" : L"0") << std::endl;
        exitCode = 0;
        return true;
    } else if (wcscmp(argv[i], L"--print-is-recognized-playlist") == 0 && i + 2 < argc) {
        std::wstring path = argv[++i];
        std::wstring textFilePath = argv[++i];
        std::ifstream file(WideToUtf8(textFilePath), std::ios::binary);
        std::ostringstream ss;
        ss << file.rdbuf();
        std::cout << (IsRecognizedPlaylistText(path, ss.str()) ? "1" : "0") << std::endl;
        exitCode = 0;
        return true;
    } else if (wcscmp(argv[i], L"--print-parse-quoted-comma-list") == 0 && i + 1 < argc) {
        for (const auto& field : ParseQuotedCommaList(argv[++i])) {
            std::wcout << field << std::endl;
        }
        exitCode = 0;
        return true;
    } else if (wcscmp(argv[i], L"--print-trim-wide") == 0 && i + 1 < argc) {
        std::wcout << TrimWide(argv[++i]) << std::endl;
        exitCode = 0;
        return true;
    } else if (wcscmp(argv[i], L"--print-decode-legacy-pipe-sources") == 0 && i + 1 < argc) {
        for (const auto& field : DecodeLegacyPipeDelimitedSources(argv[++i])) {
            std::wcout << field << std::endl;
        }
        exitCode = 0;
        return true;
    } else if (wcscmp(argv[i], L"--print-resolve-relative-url") == 0 && i + 2 < argc) {
        std::wstring baseUrl = argv[++i];
        std::wstring relativeUrl = argv[++i];
        std::wcout << ResolveRelativeUrl(baseUrl, relativeUrl) << std::endl;
        exitCode = 0;
        return true;
    } else if (wcscmp(argv[i], L"--print-media-resource-url-suffix") == 0 && i + 1 < argc) {
        std::cout << BuildMediaResourceUrlExtensionSuffix(argv[++i]) << std::endl;
        exitCode = 0;
        return true;
    } else if (wcscmp(argv[i], L"--print-movie-title-from-path") == 0 && i + 1 < argc) {
        std::wstring path = argv[++i];
        std::wstring stem = SourceStemName(path);
        std::wcout << (stem.empty() ? L"Media item" : stem) << std::endl;
        exitCode = 0;
        return true;
    } else if (wcscmp(argv[i], L"--print-strip-resource-id-extension") == 0 && i + 1 < argc) {
        std::cout << StripResourceIdExtension(WideToUtf8(argv[++i])) << std::endl;
        exitCode = 0;
        return true;
    } else if (wcscmp(argv[i], L"--print-media-display-title") == 0 && i + 3 < argc) {
        bool showFileNames = wcscmp(argv[++i], L"1") == 0;
        std::wstring titleOverride = argv[++i];
        std::wstring path = argv[++i];
        std::wcout << BuildDisplayTitleForMediaFile(showFileNames, titleOverride, path) << std::endl;
        exitCode = 0;
        return true;
    } else if (wcscmp(argv[i], L"--print-rewrite-hls-manifest") == 0 && i + 2 < argc) {
        std::wstring baseUrl = argv[++i];
        std::wstring textFilePath = argv[++i];
        std::ifstream file(WideToUtf8(textFilePath), std::ios::binary);
        std::ostringstream ss;
        ss << file.rdbuf();
        std::cout << RewriteHlsManifestUrisToAbsolute(baseUrl, ss.str()) << std::endl;
        exitCode = 0;
        return true;
    } else if (wcscmp(argv[i], L"--print-should-start-headless") == 0 && i + 2 < argc) {
        bool explicitFlag = wcscmp(argv[++i], L"1") == 0;
        bool hasSources = wcscmp(argv[++i], L"1") == 0;
        std::wcout << (ShouldStartHeadless(explicitFlag, hasSources) ? L"1" : L"0") << std::endl;
        exitCode = 0;
        return true;
    } else if (wcscmp(argv[i], L"--print-should-close-now") == 0 && i + 2 < argc) {
        bool isRunning = wcscmp(argv[++i], L"1") == 0;
        bool isBusy = wcscmp(argv[++i], L"1") == 0;
        std::wcout << (ShouldCloseNow(isRunning, isBusy) ? L"1" : L"0") << std::endl;
        exitCode = 0;
        return true;
    } else if (wcscmp(argv[i], L"--print-should-treat-server-unhealthy") == 0 && i + 3 < argc) {
        bool isRunning = wcscmp(argv[++i], L"1") == 0;
        bool isHealthy = wcscmp(argv[++i], L"1") == 0;
        int consecutiveUnhealthyPolls = _wtoi(argv[++i]);
        std::wcout << (ShouldTreatServerAsUnhealthy(isRunning, isHealthy, consecutiveUnhealthyPolls) ? L"1" : L"0") << std::endl;
        exitCode = 0;
        return true;
    } else if (wcscmp(argv[i], L"--print-is-plausible-copydata-size") == 0 && i + 1 < argc) {
        unsigned long cbData = wcstoul(argv[++i], nullptr, 10);
        std::wcout << (IsPlausibleWideStringCopyDataSize(cbData) ? L"1" : L"0") << std::endl;
        exitCode = 0;
        return true;
    } else if (wcscmp(argv[i], L"--print-media-database-id-reuse-lifecycle") == 0) {
        MediaDatabase db;
        std::wstring dbPath = L"test-media-cache-reuse.tsv";
        db.Load(dbPath);
        db.BeginScanPass();
        int idA = db.GetOrCreateStableId(L"key-a");
        int idB = db.GetOrCreateStableId(L"key-b");
        int idC = db.GetOrCreateStableId(L"key-c");
        std::wcout << L"initial=" << idA << L"," << idB << L"," << idC << std::endl;
        db.BeginScanPass();
        db.GetOrCreateStableId(L"key-a");
        db.GetOrCreateStableId(L"key-c");
        size_t pruned = db.PruneUntouched();
        std::wcout << L"pruned=" << pruned << std::endl;
        db.BeginScanPass();
        db.GetOrCreateStableId(L"key-a");
        db.GetOrCreateStableId(L"key-c");
        int idD = db.GetOrCreateStableId(L"key-d");
        std::wcout << L"reused=" << idD << std::endl;
        exitCode = 0;
        return true;
    } else if (wcscmp(argv[i], L"--print-media-database-id-overflow-guard") == 0 && i + 1 < argc) {
        MediaDatabase db;
        db.Load(argv[++i]);
        db.BeginScanPass();
        int idA = db.GetOrCreateStableId(L"new-key-a");
        int idB = db.GetOrCreateStableId(L"new-key-b");
        std::wcout << idA << std::endl;
        std::wcout << idB << std::endl;
        exitCode = 0;
        return true;
    } else if (wcscmp(argv[i], L"--print-clamp-browse-requested-count") == 0 && i + 2 < argc) {
        int requestedCount = _wtoi(argv[++i]);
        int available = _wtoi(argv[++i]);
        std::cout << ClampBrowseRequestedCount(requestedCount, available) << std::endl;
        exitCode = 0;
        return true;
    } else if (wcscmp(argv[i], L"--print-should-drop-link-local-endpoint") == 0 && i + 2 < argc) {
        bool candidateIsLinkLocal = wcscmp(argv[++i], L"1") == 0;
        bool anyNonLinkLocalExists = wcscmp(argv[++i], L"1") == 0;
        std::wcout << (ShouldDropLinkLocalEndpoint(candidateIsLinkLocal, anyNonLinkLocalExists) ? L"1" : L"0") << std::endl;
        exitCode = 0;
        return true;
    } else if (wcscmp(argv[i], L"--print-should-evict-before-cache-insert") == 0 && i + 2 < argc) {
        size_t currentSize = static_cast<size_t>(_wtoi64(argv[++i]));
        size_t capacity = static_cast<size_t>(_wtoi64(argv[++i]));
        std::wcout << (ShouldEvictBeforeCacheInsert(currentSize, capacity) ? L"1" : L"0") << std::endl;
        exitCode = 0;
        return true;
    } else if (wcscmp(argv[i], L"--print-transmitfile-chunk-plan") == 0 && i + 1 < argc) {
        long long totalBytes = _wtoi64(argv[++i]);
        for (long long chunkSize : ComputeTransmitFileChunkSizes(totalBytes)) {
            std::wcout << chunkSize << std::endl;
        }
        exitCode = 0;
        return true;
    } else if (wcscmp(argv[i], L"--print-function-key-action") == 0 && i + 4 < argc) {
        int vkCode = _wtoi(argv[++i]);
        bool isRunning = wcscmp(argv[++i], L"1") == 0;
        bool isBusy = wcscmp(argv[++i], L"1") == 0;
        bool isScanning = wcscmp(argv[++i], L"1") == 0;
        switch (DecideFunctionKeyAction(vkCode, isRunning, isBusy, isScanning)) {
        case FunctionKeyAction::ShowHelp: std::wcout << L"show-help" << std::endl; break;
        case FunctionKeyAction::Rescan: std::wcout << L"rescan" << std::endl; break;
        case FunctionKeyAction::RefreshSourceList: std::wcout << L"refresh-source-list" << std::endl; break;
        case FunctionKeyAction::ShowSourceListContextMenu: std::wcout << L"show-context-menu" << std::endl; break;
        case FunctionKeyAction::NoAction: std::wcout << L"none" << std::endl; break;
        }
        exitCode = 0;
        return true;
    } else if (wcscmp(argv[i], L"--print-debug-log-requires-restart") == 0 && i + 2 < argc) {
        ConfigSnapshot before{};
        ConfigSnapshot after{};
        before.debugLog = wcscmp(argv[++i], L"1") == 0;
        after.debugLog = wcscmp(argv[++i], L"1") == 0;
        std::vector<std::wstring> changed = DetermineSettingsRequiringRestart(before, after);
        bool found = false;
        for (const auto& name : changed) {
            if (name == L"Debug Log") found = true;
        }
        std::wcout << (found ? L"1" : L"0") << std::endl;
        exitCode = 0;
        return true;
    } else if (wcscmp(argv[i], L"--print-media-browsing-restart-required") == 0 && i + 2 < argc) {
        auto parseFlags = [](const std::wstring& bits, ConfigSnapshot& snap) {
            snap.addArtistAlbumFolders        = bits.size() > 0 && bits[0] == L'1';
            snap.doNotShowAllMediaFolders     = bits.size() > 1 && bits[1] == L'1';
            snap.sortByTitle                  = bits.size() > 2 && bits[2] == L'1';
            snap.flatFolderStyle              = bits.size() > 3 && bits[3] == L'1';
            snap.showFileNamesInsteadOfTitles = bits.size() > 4 && bits[4] == L'1';
            snap.proxyStreams                 = bits.size() > 5 && bits[5] == L'1';
            snap.backgroundScanEnabled        = bits.size() > 6 && bits[6] == L'1';
        };
        ConfigSnapshot before{};
        ConfigSnapshot after{};
        parseFlags(argv[++i], before);
        parseFlags(argv[++i], after);
        std::vector<std::wstring> changed = DetermineSettingsRequiringRestart(before, after);
        std::wcout << (changed.empty() ? L"0" : L"1") << std::endl;
        exitCode = 0;
        return true;
    } else if (wcscmp(argv[i], L"--print-should-allow-source-drop") == 0 && i + 1 < argc) {
        bool busyOrRunning = wcscmp(argv[++i], L"1") == 0;
        std::wcout << (ShouldAllowSourceDrop(busyOrRunning) ? L"1" : L"0") << std::endl;
        exitCode = 0;
        return true;
    } else if (wcscmp(argv[i], L"--print-is-supported-source-path") == 0 && i + 1 < argc) {
        std::wcout << (IsSupportedLocalMediaOrPlaylistPath(argv[++i]) ? L"1" : L"0") << std::endl;
        exitCode = 0;
        return true;
    } else if (wcscmp(argv[i], L"--print-routable-host-url-twice") == 0 && i + 2 < argc) {
        int portOne = 0;
        int portTwo = 0;
        if (!TryParsePortStrict(WideToUtf8(argv[++i]), portOne)) portOne = 0;
        if (!TryParsePortStrict(WideToUtf8(argv[++i]), portTwo)) portTwo = 0;
        std::string first = GetRoutableHostUrl(portOne, L"");
        std::string second = GetRoutableHostUrl(portTwo, L"");
        std::cout << first << std::endl;
        std::cout << second << std::endl;
        exitCode = 0;
        return true;
    } else if (wcscmp(argv[i], L"--print-routable-host-cache-invalidation") == 0) {
        long before = GetRoutableHostUrlRecomputeCountForTest();
        GetRoutableHostUrl(9200, L"");
        long afterFirst = GetRoutableHostUrlRecomputeCountForTest();
        GetRoutableHostUrl(9200, L"");
        long afterSecondSamePort = GetRoutableHostUrlRecomputeCountForTest();
        InvalidateRoutableHostUrlCache();
        GetRoutableHostUrl(9200, L"");
        long afterInvalidate = GetRoutableHostUrlRecomputeCountForTest();
        std::wcout << L"before=" << before << std::endl;
        std::wcout << L"after-first-call=" << afterFirst << std::endl;
        std::wcout << L"after-second-call-same-port=" << afterSecondSamePort << std::endl;
        std::wcout << L"after-invalidate-then-call=" << afterInvalidate << std::endl;
        exitCode = 0;
        return true;
    } else if (wcscmp(argv[i], L"--print-should-use-unlisted-interface") == 0 && i + 2 < argc) {
        bool isVirtual = wcscmp(argv[++i], L"1") == 0;
        bool hasGateway = wcscmp(argv[++i], L"1") == 0;
        std::wcout << (ShouldUseUnlistedInterface(isVirtual, hasGateway) ? L"1" : L"0") << std::endl;
        exitCode = 0;
        return true;
    } else if (wcscmp(argv[i], L"--print-network-endpoint-count") == 0 && i + 1 < argc) {
        int testPort = 0;
        if (!TryParsePortStrict(WideToUtf8(argv[++i]), testPort)) testPort = 8200;
        std::vector<NetworkEndpoint> endpoints;
        EnumerateNetworkEndpoints(testPort, L"", endpoints);
        std::cout << endpoints.size() << std::endl;
        exitCode = 0;
        return true;
    } else if (wcscmp(argv[i], L"--print-sockaddr-length-safety") == 0 && i + 2 < argc) {
        int reportedLength = _wtoi(argv[++i]);
        int destinationCapacity = _wtoi(argv[++i]);
        std::wcout << (IsSockaddrLengthSafeToCopy(reportedLength, static_cast<size_t>(destinationCapacity)) ? L"1" : L"0") << std::endl;
        exitCode = 0;
        return true;
    } else if (wcscmp(argv[i], L"--print-notify-pool-worker-count") == 0) {
        std::cout << kMaxUpnpNotifyWorkers << std::endl;
        exitCode = 0;
        return true;
    } else if (wcscmp(argv[i], L"--print-max-client-threads") == 0) {
        std::wcout << kMaxClientThreads << std::endl;
        exitCode = 0;
        return true;
    } else if (wcscmp(argv[i], L"--print-remote-probe-cache-lifecycle") == 0 && i + 1 < argc) {
        std::wstring probeUrl = argv[++i];
        long before = GetRemoteProbeRecomputeCountForTest();
        ProbeRemoteContentLength(probeUrl);
        long afterFirst = GetRemoteProbeRecomputeCountForTest();
        ProbeRemoteContentLength(probeUrl);
        long afterSecond = GetRemoteProbeRecomputeCountForTest();
        std::cout << "before=" << before << std::endl;
        std::cout << "after-first-probe=" << afterFirst << std::endl;
        std::cout << "after-second-probe=" << afterSecond << std::endl;
        exitCode = 0;
        return true;
    } else if (wcscmp(argv[i], L"--print-dlna-server-header") == 0) {
        std::cout << GetDlnaServerHeader() << std::endl;
        exitCode = 0;
        return true;
    } else if (wcscmp(argv[i], L"--print-ssdp-response-delay-bound") == 0 && i + 1 < argc) {
        int mx = _wtoi(argv[++i]);
        std::cout << ComputeMaxDelayMilliseconds(mx) << std::endl;
        exitCode = 0;
        return true;
    } else if (wcscmp(argv[i], L"--print-ssdp-search-response") == 0) {
        SsdpSearchResponseFields fields;
        fields.date = "Sat, 20 Sep 2026 00:00:00 GMT";
        fields.serverHeader = GetDlnaServerHeader();
        fields.locationUrl = "http://192.0.2.10:8200/description.xml";
        fields.st = "urn:schemas-upnp-org:device:MediaServer:1";
        fields.usn = "uuid:test::urn:schemas-upnp-org:device:MediaServer:1";
        fields.bootId = 1234;
        fields.configId = 1;
        std::cout << BuildSearchResponseMessage(fields);
        exitCode = 0;
        return true;
    } else if (wcscmp(argv[i], L"--print-ssdp-search-response-send-count") == 0) {
        std::cout << kSearchResponseSendCount << std::endl;
        exitCode = 0;
        return true;
    } else if (wcscmp(argv[i], L"--print-config-path") == 0) {
        std::wcout << AppConfig.GetConfigPath() << std::endl;
        exitCode = 0;
        return true;
    } else if (wcscmp(argv[i], L"--print-default-playlist-path") == 0) {
        std::wcout << AppConfig.GetDefaultPlaylistPath() << std::endl;
        exitCode = 0;
        return true;
    } else if (wcscmp(argv[i], L"--print-ssdp-alive-interval-bounds") == 0) {
        unsigned int minSeen = UINT32_MAX;
        unsigned int maxSeen = 0;
        for (int j = 0; j < 2000; ++j) {
            unsigned int sample = ComputeSsdpNextAliveIntervalMilliseconds();
            if (sample < minSeen) minSeen = sample;
            if (sample > maxSeen) maxSeen = sample;
        }
        std::cout << "min-ms=" << minSeen << std::endl;
        std::cout << "max-ms=" << maxSeen << std::endl;
        exitCode = 0;
        return true;
    } else if (wcscmp(argv[i], L"--print-source-scan-pool-worker-count") == 0) {
        std::cout << SourceScanPool::Get().WorkerCount() << std::endl;
        exitCode = 0;
        return true;
    } else if (wcscmp(argv[i], L"--print-media-source-file-extensions") == 0) {
        for (const auto& ext : GetMediaSourceFileExtensions()) {
            std::wcout << ext << std::endl;
        }
        exitCode = 0;
        return true;
    } else if (wcscmp(argv[i], L"--print-network-interface-allow-list-accessor") == 0) {
        AppConfig.Mutate([](Config& cfg) {
            cfg.networkInterfaceAllowList = L"eth0,wlan0";
        });
        std::wcout << AppConfig.GetNetworkInterfaceAllowList() << std::endl;
        exitCode = 0;
        return true;
    } else if (wcscmp(argv[i], L"--print-media-format-lookup") == 0 && i + 1 < argc) {
        std::wstring extArg = argv[++i];
        MediaFormatInfo info;
        if (!GetMediaFormatForExtension(extArg, info)) {
            std::wcout << L"no-match" << std::endl;
        } else {
            std::wcout << info.mimeType << std::endl;
            std::wcout << info.upnpClass << std::endl;
            std::wcout << Utf8ToWide(info.dlnaProfile) << std::endl;
        }
        exitCode = 0;
        return true;
    } else if (wcscmp(argv[i], L"--print-media-sources") == 0) {
        auto snap = AppConfig.Snapshot();
        for (const auto& src : snap.mediaSources) {
            std::wcout << src.path << std::endl;
        }
        exitCode = 0;
        return true;
    } else if (wcscmp(argv[i], L"--print-effective-media-sources") == 0) {
        HWND hwndExisting = FindWindowW(L"dlna-server_Main", NULL);
        if (hwndExisting) {
            wchar_t tempPath[MAX_PATH] = {0};
            wchar_t tempFile[MAX_PATH] = {0};
            if (GetTempPathW(MAX_PATH, tempPath) &&
                GetTempFileNameW(tempPath, L"dlnasrc", 0, tempFile)) {
                COPYDATASTRUCT cds{};
                cds.dwData = MainWindow::kCopyDataQueryEffectiveSources;
                cds.cbData = static_cast<DWORD>((wcslen(tempFile) + 1) * sizeof(wchar_t));
                cds.lpData = tempFile;
                SendMessageW(hwndExisting, WM_COPYDATA, 0, reinterpret_cast<LPARAM>(&cds));
                std::ifstream in(tempFile);
                std::string line;
                while (std::getline(in, line)) {
                    if (!line.empty()) std::wcout << Utf8ToWide(line) << std::endl;
                }
                DeleteFileW(tempFile);
                exitCode = 0;
                return true;
            }
        }
        auto snap = AppConfig.Snapshot();
        for (const auto& src : snap.effectiveMediaSources) {
            std::wcout << src.path << std::endl;
        }
        exitCode = 0;
        return true;
    } else if (wcscmp(argv[i], L"--print-clear-override-then-effective") == 0) {
        AppConfig.ClearRuntimeSourceOverride();
        auto snap = AppConfig.Snapshot();
        for (const auto& src : snap.effectiveMediaSources) {
            std::wcout << src.path << std::endl;
        }
        exitCode = 0;
        return true;
    } else if (wcscmp(argv[i], L"--print-source-override-lifecycle") == 0 && i + 1 < argc) {
        std::vector<std::wstring> parsedSources = ParseQuotedCommaList(argv[++i]);
        std::vector<MediaSource> overrideSources;
        for (auto& parsed : parsedSources) {
            if (!parsed.empty()) overrideSources.push_back({parsed});
        }
        AppConfig.SetRuntimeSourceOverride(overrideSources);

        std::wstring reason;
        if (!DLNAServer.Start(reason)) {
            std::wcerr << L"start1 failed: " << reason << std::endl;
            exitCode = 1;
            return true;
        }
        while (DLNAServer.IsInitialScanInProgress()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
        std::wcout << L"--override-active--" << std::endl;
        for (auto& s : AppConfig.Snapshot().effectiveMediaSources) std::wcout << s.path << std::endl;

        DLNAServer.Stop();
        std::wcout << L"--after-stop--" << std::endl;
        for (auto& s : AppConfig.Snapshot().effectiveMediaSources) std::wcout << s.path << std::endl;
        exitCode = 0;
        return true;
    } else if (wcscmp(argv[i], L"--print-concurrent-start-rescan-safety") == 0) {
        auto _dbg = [](const char* tag) {
            std::cerr << "[DBGCONC] " << tag << std::endl;
        };
        auto waitLeafCount = [&]() -> int {
            while (DLNAServer.IsInitialScanInProgress()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
            int n = 0;
            for (const auto& item : AppMedia.GetDescendants(0)) {
                if (!item.isFolder) ++n;
            }
            return n;
        };

        _dbg("A-start");
        {
            std::wstring reason;
            bool startOk = DLNAServer.Start(reason);
            _dbg("A-start-done");
            auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
            while (!DLNAServer.IsRunning() &&
                   std::chrono::steady_clock::now() < deadline) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            std::thread rescanThreadA([]() { DLNAServer.Rescan(); });
            _dbg("A-rescan-launched");
            rescanThreadA.join();
            _dbg("A-rescan-joined");
            int leafA = waitLeafCount();
            DLNAServer.Stop();
            _dbg("A-stop-done");
            std::wcout << L"subtest-a-start-ok=" << (startOk ? L"1" : L"0") << std::endl;
            std::wcout << L"subtest-a-leaf-media-items=" << leafA << std::endl;
        }

        _dbg("B-start");
        {
            std::thread rescanThreadB([]() { DLNAServer.Rescan(); });
            _dbg("B-rescan-launched");
            std::wstring reason;
            bool startOk = DLNAServer.Start(reason);
            _dbg("B-start-done");
            rescanThreadB.join();
            _dbg("B-rescan-joined");
            int leafB = waitLeafCount();
            DLNAServer.Stop();
            _dbg("B-stop-done");
            std::wcout << L"subtest-b-start-ok=" << (startOk ? L"1" : L"0") << std::endl;
            std::wcout << L"subtest-b-leaf-media-items=" << leafB << std::endl;
        }

        std::wcout << L"done" << std::endl;
        exitCode = 0;
        return true;
    } else if (wcscmp(argv[i], L"--print-concurrent-start-start-safety") == 0) {
        std::wstring reasonA;
        std::wstring reasonB;
        bool startOkA = false;
        bool startOkB = false;
        std::thread threadA([&]() { startOkA = DLNAServer.Start(reasonA); });
        std::thread threadB([&]() { startOkB = DLNAServer.Start(reasonB); });
        threadA.join();
        threadB.join();
        while (DLNAServer.IsInitialScanInProgress()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        const bool exactlyOneSucceeded = (startOkA != startOkB) || (startOkA && startOkB);
        std::wcout << L"a-ok=" << (startOkA ? L"1" : L"0") << std::endl;
        std::wcout << L"b-ok=" << (startOkB ? L"1" : L"0") << std::endl;
        std::wcout << L"a-reason=" << reasonA << std::endl;
        std::wcout << L"b-reason=" << reasonB << std::endl;
        std::wcout << L"is-running=" << (DLNAServer.IsRunning() ? L"1" : L"0") << std::endl;
        (void)exactlyOneSucceeded;
        DLNAServer.Stop();
        std::wcout << L"after-stop-running=" << (DLNAServer.IsRunning() ? L"1" : L"0") << std::endl;
        exitCode = 0;
        return true;
    } else if (wcscmp(argv[i], L"--print-ssdp-concurrent-start-stop-safety") == 0) {
        std::wstring reason;
        bool startOk = DLNAServer.Start(reason);
        std::wcout << L"start-ok=" << (startOk ? L"1" : L"0") << std::endl;
        while (DLNAServer.IsInitialScanInProgress()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        std::atomic<int> rejectedCount(0);
        auto restartOnce = [&]() {
            SSDP::Get().Stop();
            const ConfigSnapshot cfg = AppConfig.Snapshot();
            const bool ok = SSDP::Get().Start(DLNAServer.GetEndpoints(), cfg.port, cfg.serverName, cfg.deviceUUID);
            if (!ok) rejectedCount.fetch_add(1);
        };
        std::thread threadA(restartOnce);
        std::thread threadB([&]() {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            restartOnce();
        });
        threadA.join();
        threadB.join();
        std::wcout << L"rejected-count=" << rejectedCount.load() << std::endl;
        std::wcout << L"final-running=" << (DLNAServer.IsRunning() ? L"1" : L"0") << std::endl;
        DLNAServer.Stop();
        exitCode = 0;
        return true;
    } else if (wcscmp(argv[i], L"--print-httpserver-concurrent-start-stop-safety") == 0) {
        std::wstring reason;
        bool startOk = DLNAServer.Start(reason);
        std::wcout << L"start-ok=" << (startOk ? L"1" : L"0") << std::endl;
        while (DLNAServer.IsInitialScanInProgress()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        const ConfigSnapshot cfg = AppConfig.Snapshot();
        std::atomic<int> rejectedCount(0);
        auto cycleOnce = [&]() {
            HttpServer::Get().Stop();
            const bool ok = HttpServer::Get().Start(cfg.port);
            if (!ok) rejectedCount.fetch_add(1);
        };
        std::thread threadA(cycleOnce);
        std::thread threadB([&]() {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            cycleOnce();
        });
        threadA.join();
        threadB.join();
        HttpServer::Get().Start(cfg.port);
        std::wcout << L"rejected-count=" << rejectedCount.load() << std::endl;
        std::wcout << L"is-healthy=" << (HttpServer::Get().IsHealthy() ? L"1" : L"0") << std::endl;
        DLNAServer.Stop();
        exitCode = 0;
        return true;
    } else if (wcscmp(argv[i], L"--print-ssdp-bootid-persistence") == 0) {
        std::wstring reason;
        bool startOk = DLNAServer.Start(reason);
        std::wcout << L"start-ok=" << (startOk ? L"1" : L"0") << std::endl;
        while (DLNAServer.IsInitialScanInProgress()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        const unsigned int firstBootId = SSDP::Get().GetBootIdForTest();
        DLNAServer.Stop();
        bool startOk2 = DLNAServer.Start(reason);
        std::wcout << L"start-ok2=" << (startOk2 ? L"1" : L"0") << std::endl;
        while (DLNAServer.IsInitialScanInProgress()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        const unsigned int secondBootId = SSDP::Get().GetBootIdForTest();
        std::wcout << L"first=" << firstBootId << L" second=" << secondBootId << std::endl;
        DLNAServer.Stop();
        exitCode = 0;
        return true;
    } else if (wcscmp(argv[i], L"--print-network-change-restart-coalescing") == 0) {
        std::wstring reason;
        bool startOk = DLNAServer.Start(reason);
        std::wcout << L"start-ok=" << (startOk ? L"1" : L"0") << std::endl;
        while (DLNAServer.IsInitialScanInProgress()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        std::thread threadA([&]() { DLNAServer.RestartSsdpForNetworkChange(); });
        std::thread threadB([&]() { DLNAServer.RestartSsdpForNetworkChange(); });
        threadA.join();
        threadB.join();
        std::wcout << L"is-running=" << (DLNAServer.IsRunning() ? L"1" : L"0") << std::endl;
        std::wcout << L"is-healthy=" << (DLNAServer.IsHealthy() ? L"1" : L"0") << std::endl;
        DLNAServer.Stop();
        exitCode = 0;
        return true;
    } else if (wcscmp(argv[i], L"--print-network-change-restart-noop-detection") == 0) {
        std::wstring reason;
        bool startOk = DLNAServer.Start(reason);
        while (DLNAServer.IsInitialScanInProgress()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        LogSnapshot before = GetSystemLogSince(0);
        DLNAServer.RestartSsdpForNetworkChange();
        std::this_thread::sleep_for(std::chrono::milliseconds(2100));
        DLNAServer.RestartSsdpForNetworkChange();
        LogSnapshot after = GetSystemLogSince(before.latestSequence);
        const bool sawSkip = after.text.find(L"SSDP restart skipped") != std::wstring::npos ||
                             after.text.find(L"Endpoint set unchanged") != std::wstring::npos;
        const bool sawJoin = after.text.find(L"multicast join ok") != std::wstring::npos;
        std::wcout << L"start-ok=" << (startOk ? L"1" : L"0") << std::endl;
        std::wcout << L"saw-skip-message=" << (sawSkip ? L"1" : L"0") << std::endl;
        std::wcout << L"saw-fresh-multicast-join=" << (sawJoin ? L"1" : L"0") << std::endl;
        DLNAServer.Stop();
        exitCode = 0;
        return true;
    } else if (wcscmp(argv[i], L"--print-ssdp-stop-flushes-due-response") == 0) {
        AppConfig.debugLog = true;
        std::wstring reason;
        bool startOk = DLNAServer.Start(reason);
        while (DLNAServer.IsInitialScanInProgress()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        LogSnapshot before = GetSystemLogSince(0);
        SSDP::Get().QueueTestResponseDueNow();
        SSDP::Get().Stop();
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        LogSnapshot after = GetSystemLogSince(before.latestSequence);
        const bool sawResponseSent = after.text.find(L"SSDP response sent") != std::wstring::npos;
        std::wcout << L"start-ok=" << (startOk ? L"1" : L"0") << std::endl;
        std::wcout << L"ran=" << 1 << std::endl;
        std::wcout << L"saw-response-sent=" << (sawResponseSent ? L"1" : L"0") << std::endl;
        DLNAServer.Stop();
        exitCode = 0;
        return true;
    } else if (wcscmp(argv[i], L"--print-single-file-source-scan") == 0) {
        std::wstring reason;
        bool startOk = DLNAServer.Start(reason);
        if (!startOk) {
            std::wcerr << L"start failed: " << reason << std::endl;
            exitCode = 1;
            return true;
        }
        while (DLNAServer.IsInitialScanInProgress()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        int leafMediaItems = 0;
        std::wstring firstPath, firstMime, firstClass;
        for (const auto& item : AppMedia.GetDescendants(0)) {
            if (!item.isFolder) {
                ++leafMediaItems;
                if (leafMediaItems == 1) {
                    firstPath = item.path;
                    firstMime = item.mimeType;
                    firstClass = item.upnpClass;
                }
            }
        }
        std::wcout << L"leaf-media-items=" << leafMediaItems << std::endl;
        std::wcout << L"first-path=" << firstPath << std::endl;
        std::wcout << L"first-mime=" << firstMime << std::endl;
        std::wcout << L"first-class=" << firstClass << std::endl;
        DLNAServer.Stop();
        exitCode = 0;
        return true;
    } else if (wcscmp(argv[i], L"--print-search-cache-cleared-on-rescan") == 0) {
        std::wstring reason;
        bool startOk = DLNAServer.Start(reason);
        if (!startOk) {
            std::wcerr << L"start failed: " << reason << std::endl;
            exitCode = 1;
            return true;
        }
        while (DLNAServer.IsInitialScanInProgress()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        const std::string searchSoap =
            "<?xml version=\"1.0\"?>\n"
            "<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" "
            "s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\">\n"
            "  <s:Body>\n"
            "    <u:Search xmlns:u=\"urn:schemas-upnp-org:service:ContentDirectory:1\">\n"
            "      <ContainerID>0</ContainerID>\n"
            "      <SearchCriteria></SearchCriteria>\n"
            "      <Filter>*</Filter>\n"
            "      <StartingIndex>0</StartingIndex>\n"
            "      <RequestedCount>100</RequestedCount>\n"
            "      <SortCriteria></SortCriteria>\n"
            "    </u:Search>\n"
            "  </s:Body>\n"
            "</s:Envelope>\n";
        AppContent.HandleContentDirectoryControl(searchSoap, "http://127.0.0.1:0");
        std::wcout << L"before-rescan-cache-size=" << AppContent.GetSearchCacheSizeForTest() << std::endl;
        std::wcout << L"before-rescan-total-items=" << AppContent.GetSearchCacheTotalItemsForTest() << std::endl;
        DLNAServer.Rescan();
        std::wcout << L"after-rescan-cache-size=" << AppContent.GetSearchCacheSizeForTest() << std::endl;
        std::wcout << L"after-rescan-total-items=" << AppContent.GetSearchCacheTotalItemsForTest() << std::endl;
        DLNAServer.Stop();
        exitCode = 0;
        return true;
    } else if (wcscmp(argv[i], L"--print-thread-guard-behavior") == 0) {
        RunGuarded(L"test-thread", []() {
            throw std::runtime_error("synthetic-test-exception");
        });
        std::cout << "guard-caught-exception=1" << std::endl;
        exitCode = 0;
        return true;
    } else if (wcscmp(argv[i], L"--print-thread-pool-exception-resilience") == 0) {
        BoundedThreadPool pool(1);
        std::promise<bool> survived;
        std::future<bool> survivedFuture = survived.get_future();
        pool.Submit([]() {
            throw std::runtime_error("synthetic-pool-test-exception");
        });
        pool.Submit([&survived]() {
            survived.set_value(true);
        });
        bool ok = survivedFuture.wait_for(std::chrono::seconds(5)) == std::future_status::ready
                   && survivedFuture.get();
        std::wcout << L"pool-survived-exception=" << (ok ? 1 : 0) << std::endl;
        exitCode = 0;
        return true;
    } else if (wcscmp(argv[i], L"--print-tray-notify-decode") == 0 && i + 2 < argc) {
        unsigned long rawLParam = static_cast<unsigned long>(wcstoul(argv[++i], nullptr, 0));
        unsigned short expectedIconId = static_cast<unsigned short>(_wtoi(argv[++i]));
        switch (DecodeTrayNotifyEvent(rawLParam, expectedIconId)) {
        case TrayNotifyAction::Activate: std::cout << "activate" << std::endl; break;
        case TrayNotifyAction::ShowMenu: std::cout << "showmenu" << std::endl; break;
        case TrayNotifyAction::None: std::cout << "none" << std::endl; break;
        }
        exitCode = 0;
        return true;
    } else if (wcscmp(argv[i], L"--print-debug-log-session-truncation") == 0 && i + 1 < argc) {
        std::wstring path = argv[++i];
        {
            FILE* leftover = nullptr;
            _wfopen_s(&leftover, path.c_str(), L"w,ccs=UTF-8");
            if (leftover) {
                fwprintf(leftover, L"leftover line from a previous session\r\n");
                fclose(leftover);
            }
        }
        FILE* first = OpenOrReuseDebugLogFile(path);
        if (first) {
            fwprintf(first, L"session line one\r\n");
            fflush(first);
        }
        FILE* second = OpenOrReuseDebugLogFile(path);
        if (second) {
            fwprintf(second, L"session line two\r\n");
            fflush(second);
        }
        std::wcout << (first == second ? L"same-handle-reused=1" : L"same-handle-reused=0") << std::endl;
        exitCode = 0;
        return true;
    } else if (wcscmp(argv[i], L"--print-media-item-stays-resolvable-during-rescan") == 0) {
        std::wstring reason;
        bool startOk = DLNAServer.Start(reason);
        while (DLNAServer.IsInitialScanInProgress()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        int targetId = -1;
        for (const auto& item : AppMedia.GetDescendants(0)) {
            if (!item.isFolder) { targetId = item.id; break; }
        }
        std::wcout << L"start-ok=" << (startOk ? L"1" : L"0") << std::endl;
        std::wcout << L"target-id=" << targetId << std::endl;
        if (targetId == -1) {
            DLNAServer.Stop();
            exitCode = 1;
            return true;
        }
        std::atomic<bool> rescanDone(false);
        std::thread rescanThread([&rescanDone]() {
            DLNAServer.Rescan();
            rescanDone.store(true);
        });
        bool everMissing = false;
        while (!rescanDone.load()) {
            if (AppMedia.GetItem(targetId).id == -1) {
                everMissing = true;
            }
        }
        if (AppMedia.GetItem(targetId).id == -1) {
            everMissing = true;
        }
        rescanThread.join();
        std::wcout << L"ever-missing=" << (everMissing ? L"1" : L"0") << std::endl;
        DLNAServer.Stop();
        exitCode = 0;
        return true;
    } else if (wcscmp(argv[i], L"--print-server-health-detects-http-death") == 0) {
        std::wstring reason;
        bool startOk = DLNAServer.Start(reason);
        while (DLNAServer.IsInitialScanInProgress()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        std::wcout << L"start-ok=" << (startOk ? L"1" : L"0") << std::endl;
        std::wcout << L"healthy-before=" << (DLNAServer.IsHealthy() ? L"1" : L"0") << std::endl;
        HttpServer::Get().Stop();
        std::wcout << L"running-after-http-death=" << (DLNAServer.IsRunning() ? L"1" : L"0") << std::endl;
        std::wcout << L"healthy-after-http-death=" << (DLNAServer.IsHealthy() ? L"1" : L"0") << std::endl;
        DLNAServer.Stop();
        exitCode = 0;
        return true;
    } else if (wcscmp(argv[i], L"--print-modal-stack-lifecycle") == 0) {
        ModalStack<int> stack;
        std::wcout << L"empty=" << (stack.Empty() ? 1 : 0) << std::endl;
        stack.Push(0);
        std::wcout << L"after-null-push-depth=" << stack.Depth() << std::endl;
        stack.Push(1);
        stack.Push(2);
        stack.Push(3);
        std::wcout << L"top=" << stack.Top() << L" depth=" << stack.Depth() << std::endl;
        stack.Push(1);
        std::wcout << L"repush-top=" << stack.Top() << L" depth=" << stack.Depth() << std::endl;
        stack.Remove(1);
        std::wcout << L"after-remove-top=" << stack.Top() << L" depth=" << stack.Depth() << std::endl;
        stack.Remove(99);
        std::wcout << L"remove-absent-depth=" << stack.Depth() << std::endl;
        stack.Clear();
        std::wcout << L"cleared-top=" << stack.Top() << L" empty=" << (stack.Empty() ? 1 : 0) << std::endl;
        exitCode = 0;
        return true;
    } else if (wcscmp(argv[i], L"--print-close-pending-lifecycle") == 0) {
        ClosePendingState state;
        std::wcout << L"initial-pending=" << (state.IsPending() ? 1 : 0) << std::endl;

        state.RequestCloseOnceStopped();
        std::wcout << L"after-request-pending=" << (state.IsPending() ? 1 : 0) << std::endl;

        bool closeNow = state.ShouldCloseNowAfterOperation(true);
        std::wcout << L"stop-completes-close-now=" << (closeNow ? 1 : 0) << std::endl;
        std::wcout << L"after-close-pending=" << (state.IsPending() ? 1 : 0) << std::endl;

        ClosePendingState restartCase;
        restartCase.RequestCloseOnceStopped();
        bool stopAgain = restartCase.ShouldStopAgainAfterOperation(true);
        std::wcout << L"restart-reaches-running-stop-again=" << (stopAgain ? 1 : 0) << std::endl;
        std::wcout << L"restart-still-pending=" << (restartCase.IsPending() ? 1 : 0) << std::endl;
        bool closeNowAfterSecondStop = restartCase.ShouldCloseNowAfterOperation(true);
        std::wcout << L"second-stop-completes-close-now=" << (closeNowAfterSecondStop ? 1 : 0) << std::endl;

        ClosePendingState neverRequested;
        std::wcout << L"never-requested-pending=" << (neverRequested.IsPending() ? 1 : 0) << std::endl;
        exitCode = 0;
        return true;
    }

    return false;
}
